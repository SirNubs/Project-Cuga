#include "game/better_shop.hpp"
#include "psprecomp/guest_memory.hpp"

#include <array>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
constexpr std::uint32_t stock = 0x09B015E8u;
constexpr std::uint32_t pointers = 0x09B01D08u;
constexpr std::uint32_t menu = 0x09995250u;
constexpr std::array<std::uint32_t, 5> lists{stock, 0x09B01658u, 0x09B01698u, 0x09B016D8u, 0x09B01710u};
constexpr std::array<std::size_t, 5> lengths{53u, 31u, 31u, 27u, 27u};
constexpr std::array<std::uint16_t, 5> first{43u, 164u, 25u, 163u, 163u};
int failures{};
void check(bool value, const char *message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
std::vector<std::uint16_t> fixture(psprecomp::GuestMemory &m) {
    m.store32(0x09A5A580u, 0x336F574Du);
    m.store32(0x09A5A588u, 0x09A5A580u);
    constexpr char name[] = "lobby_task.ovl";
    std::memcpy(m.raw_pointer(0x09A5A5A0u, sizeof(name)), name, sizeof(name));
    m.store32(0x09A7B530u, 0x3C02089Cu);
    m.store32(0x09A7B534u, 0x8C427508u);
    m.store32(0x09A7B558u, 0x24421D08u);
    m.store32(0x09A7B564u, 0x8C420000u);
    m.store32(menu, 0u);
    std::vector<std::uint16_t> expected;
    const auto append = [&expected](std::uint16_t item) {
        for (const auto present : expected) if (present == item) return;
        expected.push_back(item);
    };
    for (std::size_t l = 0; l < lists.size(); ++l) {
        m.store32(pointers + static_cast<std::uint32_t>(l * 4u), lists[l]);
        for (std::size_t i = 0; i < lengths[l]; ++i) {
            // Synthetic overlapping inventories, with the observed boundary markers.
            const auto item = i == 0 ? first[l] : static_cast<std::uint16_t>(200u + l * 12u + i);
            m.store16(lists[l] + static_cast<std::uint32_t>(i * 2u), item);
            append(item);
        }
        m.store16(lists[l] + static_cast<std::uint32_t>(lengths[l] * 2u), 0xFFFFu);
    }
    for (const auto item : {8u, 65u, 66u}) append(static_cast<std::uint16_t>(item));
    return expected;
}
} // namespace

int main() {
    psprecomp::GuestMemory m;
    mhp2g::game::BetterShop shop;
    const auto expected = fixture(m);
    std::array<std::uint8_t, 0x160> before;
    std::memcpy(before.data(), m.raw_pointer(stock, before.size()), before.size());
    m.store32(stock - 4u, 0x12345678u);
    m.store32(stock + 0x160u, 0xABCDEF01u);
    shop.update(m, false);
    check(!shop.active() && std::memcmp(before.data(), m.raw_pointer(stock, before.size()), before.size()) == 0,
        "disabled leaves stock unchanged");
    m.store32(menu, 1u);
    shop.update(m, true);
    check(!shop.active(), "enable waits until menu closes");
    m.store32(menu, 0u);
    shop.update(m, true);
    check(shop.active(), "supported stock is enabled");
    for (std::size_t i = 0; i < expected.size(); ++i)
        check(m.load16(stock + static_cast<std::uint32_t>(i * 2u)) == expected[i], "union preserves order, removes duplicates and adds requested items");
    check(m.load16(stock + static_cast<std::uint32_t>(expected.size() * 2u)) == 0xFFFFu, "merged list terminates");
    for (std::size_t i = 0; i < lists.size(); ++i)
        check(m.load32(pointers + static_cast<std::uint32_t>(i * 4u)) == stock, "every rotation uses merged stock");
    check(m.load32(stock - 4u) == 0x12345678u && m.load32(stock + 0x160u) == 0xABCDEF01u, "neighbors untouched");
    shop.update(m, true);
    check(shop.active(), "repeated enable is stable");
    m.store32(menu, 1u);
    shop.update(m, false);
    check(shop.active(), "disable waits until menu closes");
    m.store32(menu, 0u);
    shop.update(m, false);
    check(!shop.active() && std::memcmp(before.data(), m.raw_pointer(stock, before.size()), before.size()) == 0,
        "disable restores exact stock bytes");
    for (std::size_t i = 0; i < lists.size(); ++i)
        check(m.load32(pointers + static_cast<std::uint32_t>(i * 4u)) == lists[i], "disable restores each pointer");
    shop.update(m, true);
    fixture(m); // overlay reloaded without an intervening update
    shop.update(m, true);
    check(shop.active(), "overlay reload is detected and reapplied");
    m.store32(0x09A5A580u, 0u);
    m.store16(stock, 999u);
    shop.update(m, false);
    check(!shop.active() && m.load16(stock) == 999u, "never restores into another overlay");
    fixture(m);
    m.store32(0x09A7B558u, 0u);
    shop.update(m, true);
    check(!shop.active() && m.load16(stock) == first[0], "unknown code layout is refused");
    fixture(m);
    m.store16(lists[2] + static_cast<std::uint32_t>(lengths[2] * 2u), 0u);
    shop.update(m, true);
    check(!shop.active() && m.load32(pointers + 4u) == lists[1], "bad terminator causes no partial write");
    fixture(m);
    m.store16(lists[3] + 4u, 1400u);
    shop.update(m, true);
    check(!shop.active() && m.load16(stock) == first[0], "invalid item causes no partial write");
    fixture(m);
    m.store32(pointers + 4u, 0x09000000u);
    shop.update(m, true);
    check(!shop.active(), "another mod's pointer is refused");
    fixture(m);
    shop.update(m, true);
    m.store16(stock + 2u, 999u);
    shop.update(m, false);
    check(!shop.active() && m.load16(stock + 2u) == 999u, "another mod's edit is not overwritten");
    std::cout << (failures ? "FAILED" : "PASS") << ": Better Shop synthetic memory tests\n";
    return failures ? 1 : 0;
}
