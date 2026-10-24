#pragma once

// What the host knows about the running game's memory and uses in more than
// one place: the loaded character, the game's own text, and the equipment
// kinds with their name tables. The developer tools (host/debug) and the Mods
// page (equipment_models.hpp) both read these; everything here only reads.
//
// Every address was traced in the running game (NPJB-40001, the one executable
// Yakumo supports); docs/DEBUG_MENU.md and docs/EQUIPMENT_MODS.md say how.
//
// Pure functions over a Ram, so the unit tests run them on a buffer.

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mhp3rd::game {

class Ram;

// Little-endian word at any address (the game's data is not word aligned
// throughout), and a zero-terminated UTF-8 string of at most 255 bytes.
[[nodiscard]] std::uint32_t read32(const Ram &ram, std::uint32_t address);
[[nodiscard]] std::string read_text(const Ram &ram, std::uint32_t address);

// The loaded character ----------------------------------------------------------

// The character block the game fills from the save when it enters the village
// (the game reaches it through the pointer at 0x08AB3640, plus 0x85C). It
// starts with the hunter's name, UTF-16 fullwidth as the game keeps it, 12
// characters and a terminator. The name is empty on the title screen and the
// character select, before a character is loaded.
inline constexpr std::uint32_t kCharacter = 0x09F4FCACu;
inline constexpr std::uint32_t kHunterName = kCharacter;
inline constexpr std::size_t kHunterNameUnits = 12u;

// True once a character is loaded: its name is there.
[[nodiscard]] bool character_loaded(const Ram &ram);
[[nodiscard]] std::string hunter_name(const Ram &ram);

// Text ----------------------------------------------------------------------------

// The game's text: one block the game loads at start, a header of 32-bit
// offsets to its tables, each table a list of 32-bit offsets (from the table)
// to UTF-8 strings, ended by 0xFFFFFFFF.
inline constexpr std::uint32_t kTextBlock = 0x08A40640u;

// Table `index` of the text block, or empty when it does not look like one
// (the block not loaded yet, or a different executable).
[[nodiscard]] std::vector<std::string> text_table(const Ram &ram, std::uint32_t block, int index);
// One entry of a table, or "" when there is none.
[[nodiscard]] std::string text_entry(const Ram &ram, std::uint32_t block, int index, std::uint32_t entry);
// How many entries a table has; 0 when it does not look like one.
[[nodiscard]] std::uint32_t text_count(const Ram &ram, std::uint32_t block, int index);

// Equipment kinds ------------------------------------------------------------------

// The equipment box: 1000 slots of 12 bytes: u8 1 for a used slot, u8 kind
// (EquipmentKind), u16 id, u16 armor level or weapon flags, u16[3] the item
// ids of the decorations in its slots. It ends where the item box begins.
inline constexpr std::uint32_t kEquipmentBox = 0x09F4FE14u;
inline constexpr std::uint32_t kEquipmentBoxSlots = 1000u;
inline constexpr std::uint32_t kEquipmentRecord = 12u;

// The kind byte of an equipment record, and the text table with its names.
// Kinds 0 to 4 are the armor parts (chest, arms, waist, legs, head), 5 to 17
// the weapon classes (10 is not used).
struct EquipmentKind {
    std::uint8_t kind;
    int name_table;
    const char *label;
};
// Armor parts first, then the weapon classes in the game's own order.
[[nodiscard]] const std::vector<EquipmentKind> &equipment_kinds();
[[nodiscard]] const EquipmentKind *equipment_kind(std::uint8_t kind);
[[nodiscard]] bool is_armor(std::uint8_t kind);
[[nodiscard]] bool is_weapon(std::uint8_t kind);
// The names of one kind's pieces, indexed by id. Id 0 is "no equipment".
[[nodiscard]] std::vector<std::string> equipment_names(const Ram &ram, std::uint8_t kind);
// One piece's name, or "" when there is none.
[[nodiscard]] std::string equipment_name(const Ram &ram, std::uint8_t kind, std::uint16_t id);

} // namespace mhp3rd::game
