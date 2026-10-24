#include "game/better_shop.hpp"

#include "psprecomp/guest_memory.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace mhp2g::game {
namespace {

// ULJM05500/FUC: traced in the user's village overlay. The peddler's stock
// getter at 0x09A803C0 indexes this pointer table with player data +0x44BD.
// All five lists end with u16 0xFFFF. Their contiguous data area is large
// enough for their union plus three items, without touching the next shop.
constexpr std::uint32_t kOverlay = 0x09A5F300u;
constexpr std::uint32_t kGetter = 0x09A803C0u;
constexpr std::uint32_t kPointers = 0x09B070D0u;
constexpr std::uint32_t kStock = 0x09B069B0u;
constexpr std::uint32_t kMenuUp = 0x09999650u;
constexpr std::array<std::uint32_t, 5> kLists{
    0x09B069B0u, 0x09B06A20u, 0x09B06A60u, 0x09B06AA0u, 0x09B06AD8u};
constexpr std::array<std::size_t, 5> kLengths{53u, 31u, 31u, 27u, 27u};
constexpr std::array<std::uint16_t, 5> kFirst{43u, 164u, 25u, 163u, 163u};
constexpr std::array<std::uint16_t, 3> kAdded{8u, 65u, 66u}; // Mega Potion, Flash Bomb, Sonic Bomb

bool supported(const psprecomp::GuestMemory &memory) {
    constexpr char name[] = "lobby_task.ovl";
    return memory.raw_pointer(kOverlay, 64u) != nullptr &&
        memory.raw_pointer(kGetter, 0x38u) != nullptr &&
        memory.raw_pointer(kPointers, 20u) != nullptr &&
        memory.raw_pointer(kMenuUp, 4u) != nullptr &&
        memory.load32(kOverlay) == 0x336F574Du && memory.load32(kOverlay + 8u) == kOverlay &&
        std::memcmp(memory.raw_pointer(kOverlay + 32u, sizeof(name)), name, sizeof(name)) == 0 &&
        memory.load32(kGetter) == 0x3C02089Du && memory.load32(kGetter + 4u) == 0x8C42C558u &&
        memory.load32(kGetter + 0x28u) == 0x244270D0u && memory.load32(kGetter + 0x34u) == 0x8C420000u;
}

bool pointers_match(const psprecomp::GuestMemory &memory, bool merged) {
    for (std::size_t i = 0u; i < kLists.size(); ++i)
        if (memory.load32(kPointers + static_cast<std::uint32_t>(i * 4u)) != (merged ? kStock : kLists[i]))
            return false;
    return true;
}

void set_pointers(psprecomp::GuestMemory &memory, bool merged) {
    for (std::size_t i = 0u; i < kLists.size(); ++i)
        memory.store32(kPointers + static_cast<std::uint32_t>(i * 4u), merged ? kStock : kLists[i]);
}

} // namespace

void BetterShop::update(psprecomp::GuestMemory &memory, bool enabled) {
    if (!supported(memory)) {
        active_ = false; // another overlay owns this address range; never restore into it
        return;
    }
    auto *const stock = memory.raw_pointer(kStock, kStockBytes);
    if (stock == nullptr) return;
    if (active_ && (!pointers_match(memory, true) || std::memcmp(stock, replacement_.data(), kStockBytes) != 0))
        active_ = false; // the village overlay was reloaded, or another mod changed the lists
    // A shop may retain a pointer into one of the lists. Wait for the game's
    // menu to close before merging or restoring; changes apply on reopening.
    if (memory.load32(kMenuUp) != 0u) return;
    if (active_) {
        if (!enabled) {
            std::memcpy(stock, original_.data(), kStockBytes);
            set_pointers(memory, false);
            active_ = false;
        }
        return;
    }
    if (!enabled || !pointers_match(memory, false)) return;
    std::vector<std::uint16_t> items;
    const auto append = [&items](std::uint16_t item) {
        if (std::find(items.begin(), items.end(), item) == items.end()) items.push_back(item);
    };
    for (std::size_t list = 0u; list < kLists.size(); ++list) {
        if (memory.load16(kLists[list]) != kFirst[list] ||
            memory.load16(kLists[list] + static_cast<std::uint32_t>(kLengths[list] * 2u)) != 0xFFFFu)
            return;
        for (std::size_t i = 0u; i < kLengths[list]; ++i) {
            const std::uint16_t item = memory.load16(kLists[list] + static_cast<std::uint32_t>(i * 2u));
            if (item == 0u || item >= 1400u) return;
            append(item);
        }
    }
    for (const auto item : kAdded) append(item);
    if ((items.size() + 1u) * 2u > kStockBytes) return;
    std::memcpy(original_.data(), stock, kStockBytes);
    for (std::size_t i = 0u; i < items.size(); ++i)
        memory.store16(kStock + static_cast<std::uint32_t>(i * 2u), items[i]);
    memory.store16(kStock + static_cast<std::uint32_t>(items.size() * 2u), 0xFFFFu);
    set_pointers(memory, true);
    std::memcpy(replacement_.data(), stock, kStockBytes);
    active_ = true;
}

void better_shop_frame(psprecomp::GuestMemory &memory, bool enabled) {
    static BetterShop shop;
    shop.update(memory, enabled);
}

} // namespace mhp2g::game
