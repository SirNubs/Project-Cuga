#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace psprecomp {
class GuestMemory;
}

// "Item loadouts": four named sets of what the pouch holds, kept in loadouts.txt in the data folder. Saving one
// copies the pouch as it is. Loading one puts the pouch's items back into the item box and takes the set's
// items out of it, as many of each as the box has. Only the player's own pouch and box change, with the item
// box's menu open (camera/lock_on.hpp has where those are and how the menu is told), so nothing of it concerns
// other players.
namespace mhp2g::game {

inline constexpr int kLoadouts = 4;
inline constexpr int kPouchSlots = 24;

struct Loadout {
    std::string name;                                                      // "Loadout 1" until renamed
    std::array<std::pair<std::uint16_t, std::uint16_t>, kPouchSlots> items{}; // item, count; item 0 = empty
    bool used{};
    [[nodiscard]] int kinds() const;
};

[[nodiscard]] const Loadout &loadout(int index);
void rename_loadout(int index, const std::string &name);

// Asks for a loadout to be saved from the pouch, or loaded into it. Done by loadouts_frame() at the next flip.
void request_save_loadout(int index);
void request_load_loadout(int index);

// An item's name as the game has it, for the port's notes ("item 123" when the game's list is not where it is
// in the English-patched game). The names are one run of strings, a zero after each, from 0x089D15AB.
[[nodiscard]] std::string item_name(const psprecomp::GuestMemory &memory, std::uint32_t item);
// "Potion, Cool Drink, Tranq Bomb and 2 more": up to three names of a list, for a note.
[[nodiscard]] std::string item_names(const psprecomp::GuestMemory &memory, const std::vector<std::uint32_t> &items);

// Once a flip: serves a request while the item box's menu is up. Returns a line to show, or nothing.
std::string loadouts_frame(psprecomp::GuestMemory &memory);

} // namespace mhp2g::game
