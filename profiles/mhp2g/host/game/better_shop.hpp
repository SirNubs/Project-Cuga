#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace psprecomp {
class GuestMemory;
}

namespace mhp2g::game {

// Owns a reversible edit of the village peddler's five stock lists. No save
// data or executable instructions are changed. Call only on the guest thread.
class BetterShop {
public:
    void update(psprecomp::GuestMemory &memory, bool enabled);
    [[nodiscard]] bool active() const { return active_; }

private:
    static constexpr std::size_t kStockBytes = 0x160u;
    std::array<std::uint8_t, kStockBytes> original_{};
    std::array<std::uint8_t, kStockBytes> replacement_{};
    bool active_{};
};

void better_shop_frame(psprecomp::GuestMemory &memory, bool enabled);

} // namespace mhp2g::game
