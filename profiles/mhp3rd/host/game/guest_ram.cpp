#include "game/guest_ram.hpp"

#include "psprecomp/guest_memory.hpp"
#include "psprecomp/runtime.hpp"

namespace mhp3rd::game {

bool GuestRam::contains(std::uint32_t address, std::size_t length) const { return memory_.contains(address, length); }
std::uint8_t GuestRam::load8(std::uint32_t address) const { return memory_.load8(address); }
std::uint16_t GuestRam::load16(std::uint32_t address) const { return memory_.load16(address); }
std::uint32_t GuestRam::load32(std::uint32_t address) const { return memory_.load32(address); }
void GuestRam::store8(std::uint32_t address, std::uint8_t value) { memory_.store8(address, value); }
void GuestRam::store16(std::uint32_t address, std::uint16_t value) { memory_.store16(address, value); }
void GuestRam::store32(std::uint32_t address, std::uint32_t value) { memory_.store32(address, value); }

namespace {
psprecomp::Runtime *&attached() {
    static psprecomp::Runtime *runtime = nullptr;
    return runtime;
}
} // namespace

void attach(psprecomp::Runtime &runtime) { attached() = &runtime; }

bool read(const std::function<void(const Ram &)> &reader) {
    psprecomp::Runtime *runtime = attached();
    if (runtime == nullptr) return false;
    const GuestRam ram(runtime->memory());
    reader(ram);
    return true;
}

} // namespace mhp3rd::game
