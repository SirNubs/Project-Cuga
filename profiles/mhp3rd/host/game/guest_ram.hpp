#pragma once

// The host's view of guest memory for code that reads the game's own
// structures (game_data.hpp, the developer tools, the Mods page): loads and
// stores by guest address, and a check that a range is backed by memory. The
// game runs on psprecomp::GuestMemory; the unit tests run the same functions on
// a plain buffer, so everything that reads or writes the game's structures can
// be tested without the game.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace psprecomp {
class GuestMemory;
class Runtime;
}

namespace mhp3rd::game {

class Ram {
public:
    virtual ~Ram() = default;
    [[nodiscard]] virtual bool contains(std::uint32_t address, std::size_t length) const = 0;
    [[nodiscard]] virtual std::uint8_t load8(std::uint32_t address) const = 0;
    [[nodiscard]] virtual std::uint16_t load16(std::uint32_t address) const = 0;
    [[nodiscard]] virtual std::uint32_t load32(std::uint32_t address) const = 0;
    virtual void store8(std::uint32_t address, std::uint8_t value) = 0;
    virtual void store16(std::uint32_t address, std::uint16_t value) = 0;
    virtual void store32(std::uint32_t address, std::uint32_t value) = 0;
};

// Guest memory as the running game sees it.
class GuestRam final : public Ram {
public:
    explicit GuestRam(psprecomp::GuestMemory &memory) : memory_(memory) {}
    [[nodiscard]] bool contains(std::uint32_t address, std::size_t length) const override;
    [[nodiscard]] std::uint8_t load8(std::uint32_t address) const override;
    [[nodiscard]] std::uint16_t load16(std::uint32_t address) const override;
    [[nodiscard]] std::uint32_t load32(std::uint32_t address) const override;
    void store8(std::uint32_t address, std::uint8_t value) override;
    void store16(std::uint32_t address, std::uint16_t value) override;
    void store32(std::uint32_t address, std::uint32_t value) override;

private:
    psprecomp::GuestMemory &memory_;
};

// A zero-filled buffer standing for guest memory from `base`, little endian
// like the PSP. For tests.
class BufferRam final : public Ram {
public:
    BufferRam(std::uint32_t base, std::size_t size) : base_(base), bytes_(size, 0u) {}
    [[nodiscard]] bool contains(std::uint32_t address, std::size_t length) const override {
        return address >= base_ && static_cast<std::size_t>(address - base_) + length <= bytes_.size();
    }
    [[nodiscard]] std::uint8_t load8(std::uint32_t address) const override { return bytes_[address - base_]; }
    [[nodiscard]] std::uint16_t load16(std::uint32_t address) const override {
        return static_cast<std::uint16_t>(load8(address) | (load8(address + 1u) << 8u));
    }
    [[nodiscard]] std::uint32_t load32(std::uint32_t address) const override {
        return static_cast<std::uint32_t>(load16(address)) | (static_cast<std::uint32_t>(load16(address + 2u)) << 16u);
    }
    void store8(std::uint32_t address, std::uint8_t value) override { bytes_[address - base_] = value; }
    void store16(std::uint32_t address, std::uint16_t value) override {
        store8(address, static_cast<std::uint8_t>(value));
        store8(address + 1u, static_cast<std::uint8_t>(value >> 8u));
    }
    void store32(std::uint32_t address, std::uint32_t value) override {
        store16(address, static_cast<std::uint16_t>(value));
        store16(address + 2u, static_cast<std::uint16_t>(value >> 16u));
    }

private:
    std::uint32_t base_;
    std::vector<std::uint8_t> bytes_;
};

// The running game, for the interface. attach() is called at every flip, on
// the thread that runs the game; the menu is drawn at the flip too, between two
// game frames, so read() from the menu sees memory the game is not changing.
void attach(psprecomp::Runtime &runtime);
// Calls `reader` with the game's memory; false (and no call) before the game
// has run a frame.
bool read(const std::function<void(const Ram &)> &reader);

} // namespace mhp3rd::game
