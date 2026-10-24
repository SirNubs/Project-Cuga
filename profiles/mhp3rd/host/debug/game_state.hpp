#pragma once

// What the developer tools know about the game's memory: where the running
// game keeps the hunter's money, item box and equipment box, the quest's
// state, and the functions that read and change them. What other parts of the
// host read too (the character, the game's text, the equipment kinds) is in
// game/game_data.hpp.
//
// Every address here was traced in the running game (NPJB-40001, the one
// executable Yakumo supports); docs/DEBUG_MENU.md says how each was found.
// Nothing here writes on its own: debug_tools.cpp calls these at the flip.
//
// Pure functions over a Ram, so the unit tests run them on a buffer.

#include "game/game_data.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mhp3rd::game {
class Ram;
}

namespace mhp3rd::debug {

using game::Ram;

namespace p3rd {

// The character, the text and the equipment kinds.
using namespace ::mhp3rd::game;

// Money (zenny) and the two point balances, 32-bit each.
inline constexpr std::uint32_t kPoints1 = 0x09FAC8CCu;
inline constexpr std::uint32_t kPoints2 = 0x09FAC8D0u;
inline constexpr std::uint32_t kMoney = 0x09FAC8D4u;
// The game shows seven digits.
inline constexpr std::uint32_t kMostMoney = 9'999'999u;

// The item box: 1000 slots of {u16 item id, u16 count}; id 0 is an empty slot.
inline constexpr std::uint32_t kItemBox = 0x09F52CF4u;
inline constexpr std::uint32_t kItemBoxSlots = 1000u;
inline constexpr std::uint16_t kMostPerStack = 99u;

// The game's item table in the executable: 20 bytes per item id.
inline constexpr std::uint32_t kItemData = 0x089D0FA0u;
inline constexpr std::uint32_t kItemRecord = 20u;

// The item names: text table 3, by item id.
inline constexpr int kItemNameTable = 3;

// Items -------------------------------------------------------------------------

enum class ItemGroup : std::uint8_t { Other, Material, Consumable, Ammo, Decoration };

struct ItemInfo {
    std::uint8_t category{}; // 0 items, 1 ammo and coatings, 3 decorations
    std::uint8_t rarity{};
    std::uint8_t carry{}; // how many the pouch holds
    std::uint8_t use{};   // non-zero for items that can be used
    std::uint32_t buy{};
    std::uint32_t sell{};
};

[[nodiscard]] ItemInfo item_info(const Ram &ram, std::uint16_t id);
[[nodiscard]] ItemGroup item_group(const ItemInfo &info);
[[nodiscard]] const char *group_name(ItemGroup group);

// Every item the game has: its id, name and group. Empty until the text is
// loaded.
struct Item {
    std::uint16_t id{};
    std::string name;
    ItemGroup group{};
};
[[nodiscard]] std::vector<Item> item_list(const Ram &ram);

struct ItemStack {
    std::uint16_t id{};
    std::uint16_t count{};
};
[[nodiscard]] std::vector<ItemStack> item_box(const Ram &ram);
// How many of an item the box holds, over all its stacks.
[[nodiscard]] std::uint32_t box_count(const Ram &ram, std::uint16_t id);
[[nodiscard]] std::uint32_t free_item_slots(const Ram &ram);

// Adds `count` of an item: tops up its stacks, then fills empty slots, 99 to a
// stack. Returns how many went in (less when the box is full).
std::uint32_t give_item(Ram &ram, std::uint16_t id, std::uint32_t count);
// Takes every stack of an item out. Returns how many were removed.
std::uint32_t remove_item(Ram &ram, std::uint16_t id);
// Gives `count` of every material the box does not hold yet, while there are
// free slots. Returns how many kinds were added.
std::uint32_t fill_materials(Ram &ram, std::uint16_t count);

// Equipment ---------------------------------------------------------------------

struct Equipment {
    std::uint8_t kind{};
    std::uint16_t id{};
    std::uint16_t level{};
};
[[nodiscard]] std::vector<std::optional<Equipment>> equipment_box(const Ram &ram);
[[nodiscard]] std::uint32_t free_equipment_slots(const Ram &ram);
// Puts one piece in the first free slot of the equipment box, new and without
// decorations. Returns the slot, or nullopt when the box is full.
std::optional<std::uint32_t> give_equipment(Ram &ram, std::uint8_t kind, std::uint16_t id);

// On a quest ---------------------------------------------------------------------

// The code overlay in the task slot names the game's mode: game_task.ovl while
// a quest runs, lobby_task.ovl in the village and the Guild Hall. The player
// and monster addresses below hold other things outside a quest, so nothing
// here reads or writes them unless the quest overlay is loaded.
inline constexpr std::uint32_t kTaskSlot = 0x0A05E600u;
[[nodiscard]] bool on_quest(const Ram &ram);

// The hunter on a quest: health (s16) now, the red part it can still recover
// to, and its most; stamina now (a float) and its most (u16), both in the
// game's units (150 shown is 900).
inline constexpr std::uint32_t kHealth = 0x09649B16u;
inline constexpr std::uint32_t kRecoverableHealth = 0x09649B56u;
inline constexpr std::uint32_t kMostHealth = 0x09649B58u;
inline constexpr std::uint32_t kStamina = 0x09649DF0u;
inline constexpr std::uint32_t kMostStamina = 0x0964A49Au;

// The quest clock, in frames at 30 a second: its limit and what is left.
inline constexpr std::uint32_t kQuestTimeLimit = 0x09FB4E64u;
inline constexpr std::uint32_t kQuestTimeLeft = 0x09FB4E68u;

// The large monsters (and companions) on the quest: a table of pointers in the
// quest overlay's data, each to an object with its kind (u8 at +0x62), its
// health (s16 at +0x246) and its most health (s16 at +0x288).
inline constexpr std::uint32_t kMonsterTable = 0x0A1B0AE0u;
inline constexpr std::uint32_t kMonsterSlots = 5u;
inline constexpr std::uint32_t kMonsterKind = 0x62u;
inline constexpr std::uint32_t kMonsterHealth = 0x246u;
inline constexpr std::uint32_t kMonsterMostHealth = 0x288u;
// The monster names: text table 2 from this index, by kind.
inline constexpr int kMonsterNameTable = 2;
inline constexpr std::uint32_t kMonsterNameFirst = 308u;
inline constexpr std::uint32_t kMonsterKinds = 75u;

struct Monster {
    std::uint32_t address{};
    std::uint8_t kind{};
    std::int16_t health{};
    std::int16_t most{};
};
// The monsters with health, from the table; empty outside a quest.
[[nodiscard]] std::vector<Monster> monsters(const Ram &ram);
[[nodiscard]] std::string monster_name(const Ram &ram, std::uint8_t kind);

// Money -------------------------------------------------------------------------

[[nodiscard]] std::uint32_t money(const Ram &ram);
void set_money(Ram &ram, std::uint32_t zenny);

} // namespace p3rd

// Console commands for the game's own structures; false for an unknown one.
bool game_command(
    Ram &ram, const std::string &command, const std::vector<std::string> &args, std::vector<std::string> &out);

struct HeldCheats;

// Keeps the held cheats applied, once a frame.
void held_cheats_frame(Ram &ram, const HeldCheats &cheats);

// The quest in progress, as lines for the Debug page; empty outside a quest.
[[nodiscard]] std::vector<std::string> quest_lines(const Ram &ram);

} // namespace mhp3rd::debug
