#pragma once

// Which DATA.BIN file holds the model of a piece of equipment, as the game
// itself works it out, so that the Mods page can point an equipment mod at the
// armor the hunter is wearing. Read-only: nothing here writes guest memory.
//
// Traced in the running game (NPJB-40001); docs/EQUIPMENT_MODS.md says how.
// In short:
//
// - The loaded character (game_data.hpp's kCharacter) holds the hunter's sex
//   at +0x1B (0 male, 1 female) and inner wear at +0x1D, the carried weapon at
//   +0x38 (24 bytes) and the worn armor at +0x50: five 12-byte records (chest,
//   arms, waist, legs, head) laid out like the equipment box's.
// - Each armor part has a 40-byte record per id in the executable; its first
//   two u16 are the model number for a male and for a female hunter.
// - A file id is a base per sex and part (a table in the executable) plus the
//   model number. An empty chest, arms or legs slot shows the inner wear, a
//   model number from a small table by sex and inner wear (60 to 63 here); an
//   empty head or waist slot shows model 0.
// - A weapon's record starts with its model number too; its file is a base per
//   weapon class plus that number, kept below the class's model count.
//
// Pure functions over a Ram, so the unit tests run them on a buffer.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mhp2g::game {

class Ram;

// The character block's fields (offsets from kCharacter).
inline constexpr std::uint32_t kCharacterSex = 0x1Bu;
inline constexpr std::uint32_t kCharacterInnerWear = 0x1Du;
inline constexpr std::uint32_t kCharacterWeapon = 0x38u;
inline constexpr std::uint32_t kCharacterArmor = 0x50u;  // chest, arms, waist, legs, head

// The armor records: one table per part (the kind byte), 40 bytes per id: u16
// male model, u16 female model, u8 who can wear it (bit 0 male, bit 1 female,
// bit 2 blademaster, bit 3 gunner).
inline constexpr std::uint32_t kArmorRecord = 40u;
inline constexpr std::uint32_t kChestData = 0x08985A7Cu;
inline constexpr std::uint32_t kArmsData = 0x08983934u;
inline constexpr std::uint32_t kWaistData = 0x0898A6E4u;
inline constexpr std::uint32_t kLegsData = 0x0898C854u;
inline constexpr std::uint32_t kHeadData = 0x08987EE4u;

// The first file of each part's models: u16[2][7] by sex (male, female) and
// part (chest, arms, waist, legs, head, then hair and face).
inline constexpr std::uint32_t kArmorFileBase = 0x089E892Cu;
inline constexpr std::uint32_t kParts = 7u;
// The inner wear's model numbers for an empty chest, arms and legs slot: a
// pointer per sex (male, female) to s16 by inner wear.
inline constexpr std::uint32_t kChestInnerWear = 0x089E8704u;
inline constexpr std::uint32_t kArmsInnerWear = 0x089E86ECu;
inline constexpr std::uint32_t kLegsInnerWear = 0x089E871Cu;
inline constexpr std::uint8_t kInnerWears = 4u;

// The weapon classes, kind - 5 (class 5, kind 10, is unused): the first file
// of each class's models (u16 each) and how many models it has (u16, every 4
// bytes).
inline constexpr std::uint32_t kWeaponFileBase = 0x089CD22Cu;
inline constexpr std::uint32_t kWeaponModelCount = 0x089CD1F8u;
inline constexpr std::uint32_t kWeaponClasses = 13u;

enum class Sex : std::uint8_t { Male = 0, Female = 1 };
[[nodiscard]] const char *sex_name(Sex sex);

// The table of an armor part (kind 0 to 4), or 0 for other kinds.
[[nodiscard]] std::uint32_t armor_data(std::uint8_t kind);
// The model number of an armor piece for a hunter of this sex, as its record
// gives it; nothing when the record is not in memory.
[[nodiscard]] std::optional<std::uint16_t> armor_model(const Ram &ram, std::uint8_t kind, std::uint16_t id, Sex sex);
// Whether a hunter of this sex can wear the piece (its record's byte at +4).
// Pieces for the other sex have model 0 on this side.
[[nodiscard]] bool wearable_by(const Ram &ram, std::uint8_t kind, std::uint16_t id, Sex sex);

// What the hunter's own look adds to the armor: the sex and the inner wear.
struct Look {
    Sex sex{Sex::Male};
    std::uint8_t inner_wear{};
};
// The loaded character's look; nothing when no character is loaded.
[[nodiscard]] std::optional<Look> hunter_look(const Ram &ram);

struct Piece {
    std::uint8_t kind{};
    std::uint16_t id{};  // 0: nothing in that slot
};
// The armor the loaded character wears in the slot of an armor kind (0 to 4);
// id 0 when the slot is empty. Nothing when no character is loaded.
[[nodiscard]] std::optional<Piece> worn_armor(const Ram &ram, std::uint8_t kind);
// The weapon the loaded character carries.
[[nodiscard]] std::optional<Piece> carried_weapon(const Ram &ram);

// The file with a piece's model for a hunter of this look, as the game picks
// it: an empty armor slot gives the inner wear or the bare part. Nothing when
// the tables do not look right (the executable not loaded yet).
[[nodiscard]] std::optional<std::uint32_t> model_file(const Ram &ram, const Piece &piece, const Look &look);

// Whose model a file is: the equipment kind, the sex for armor, the model
// number. Nothing for other files.
struct FileModel {
    std::uint8_t kind{};
    std::optional<Sex> sex;  // armor only
    std::uint16_t model{};
};
[[nodiscard]] std::optional<FileModel> file_model(const Ram &ram, std::uint32_t file);

// A file described by what the game draws from it, with the game's own names:
// "<name>, female", "<name> and 1 more, male", "No armor (inner wear),
// female", or a weapon's "<name>". Empty for other files.
[[nodiscard]] std::string describe_file(const Ram &ram, std::uint32_t file);

// The equipment kind an equipment mod's type names, in the mod manager's keys
// (HEAD, BODY, ARMS, WAIST, LEGS, GS, LS, ...); nothing for Felyne gear and
// unknown keys.
[[nodiscard]] std::optional<std::uint8_t> kind_of_mod_part(std::string_view key);

} // namespace mhp2g::game
