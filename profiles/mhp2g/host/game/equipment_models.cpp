#include "game/equipment_models.hpp"

#include "game/game_data.hpp"
#include "game/guest_ram.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace mhp2g::game {
namespace {

// Nothing the game has comes close: a bigger number means the table is not
// there yet.
constexpr std::uint32_t kMostModels = 512u;
constexpr std::uint32_t kMostPieces = 1024u;

std::uint32_t inner_wear_table(std::uint8_t kind) {
    switch (kind) {
    case 0: return kChestInnerWear;
    case 1: return kArmsInnerWear;
    case 3: return kLegsInnerWear;
    default: return 0u;  // waist and head show model 0
    }
}

// The weapon records: where each kind's table is, and its record size. The
// game picks the table by the kind byte; the model number is the first u16.
struct WeaponTable {
    std::uint8_t kind;
    std::uint32_t data;
    std::uint32_t record;
};
constexpr WeaponTable kWeaponTables[] = {
    {5, 0x08997AA0u, 28u},  {6, 0x089953B0u, 28u},  {7, 0x08994054u, 28u},  {8, 0x0899669Cu, 28u},
    {9, 0x08990464u, 80u},  {11, 0x08991954u, 80u}, {12, 0x08997138u, 28u}, {13, 0x08992F0Cu, 28u},
    {14, 0x08995E14u, 28u}, {15, 0x0898EB14u, 80u}, {16, 0x08994A9Cu, 28u}, {17, 0x089936ECu, 28u},
};

const WeaponTable *weapon_table(std::uint8_t kind) {
    for (const WeaponTable &t : kWeaponTables)
        if (t.kind == kind) return &t;
    return nullptr;
}

std::uint32_t character(std::uint32_t offset) { return kCharacter + offset; }

// The first file of a part's models for a sex, and how many there are (up to
// the next part's first file).
std::optional<std::pair<std::uint32_t, std::uint32_t>> armor_files(const Ram &ram, Sex sex, std::uint8_t part) {
    const std::uint32_t row = kArmorFileBase + static_cast<std::uint32_t>(sex) * kParts * 2u;
    if (part + 1u >= kParts || !ram.contains(row, kParts * 2u)) return std::nullopt;
    const std::uint32_t first = ram.load16(row + part * 2u);
    const std::uint32_t next = ram.load16(row + (part + 1u) * 2u);
    if (first == 0u || next <= first || next - first > kMostModels) return std::nullopt;
    return std::make_pair(first, next - first);
}

std::optional<std::pair<std::uint32_t, std::uint32_t>> weapon_files(const Ram &ram, std::uint8_t kind) {
    if (kind < 5u) return std::nullopt;
    const std::uint32_t weapon_class = kind - 5u;
    if (weapon_class >= kWeaponClasses || !ram.contains(kWeaponFileBase, kWeaponClasses * 2u) ||
        !ram.contains(kWeaponModelCount, kWeaponClasses * 4u))
        return std::nullopt;
    const std::uint32_t first = ram.load16(kWeaponFileBase + weapon_class * 2u);
    const std::uint32_t count = ram.load16(kWeaponModelCount + weapon_class * 4u);
    if (first == 0u || count == 0u || count > kMostModels) return std::nullopt;
    return std::make_pair(first, count);
}

std::optional<std::uint16_t> inner_wear_model(const Ram &ram, std::uint8_t kind, const Look &look) {
    const std::uint32_t table = inner_wear_table(kind);
    if (table == 0u) return std::uint16_t{0};
    if (!ram.contains(table, 8u)) return std::nullopt;
    const std::uint32_t list = ram.load32(table + static_cast<std::uint32_t>(look.sex) * 4u);
    const std::uint32_t at = list + static_cast<std::uint32_t>(std::min<std::uint8_t>(look.inner_wear, kInnerWears - 1u)) * 2u;
    if (!ram.contains(at, 2u)) return std::nullopt;
    return ram.load16(at);
}

std::optional<std::uint16_t> weapon_model(const Ram &ram, std::uint8_t kind, std::uint16_t id) {
    const WeaponTable *t = weapon_table(kind);
    if (t == nullptr) return std::nullopt;
    const std::uint32_t at = t->data + static_cast<std::uint32_t>(id) * t->record;
    if (!ram.contains(at, t->record)) return std::nullopt;
    return ram.load16(at);
}

// The names of the pieces of a kind that show this model: "Name" or "Name and
// N more".
std::string pieces_with_model(const Ram &ram, std::uint8_t kind, std::optional<Sex> sex, std::uint16_t model) {
    const EquipmentKind *k = equipment_kind(kind);
    if (k == nullptr) return {};
    const std::uint32_t count = std::min(text_count(ram, kTextBlock, k->name_table), kMostPieces);
    std::string first;
    std::size_t more = 0u;
    for (std::uint32_t id = 1; id < count; ++id) {
        std::optional<std::uint16_t> m;
        if (sex) {
            // Pieces for the other sex only have model 0 on this side.
            if (!wearable_by(ram, kind, static_cast<std::uint16_t>(id), *sex)) continue;
            m = armor_model(ram, kind, static_cast<std::uint16_t>(id), *sex);
        } else {
            m = weapon_model(ram, kind, static_cast<std::uint16_t>(id));
        }
        if (!m || *m != model) continue;
        if (first.empty()) {
            first = text_entry(ram, kTextBlock, k->name_table, id);
            if (first.empty()) continue;
        } else {
            ++more;
        }
    }
    if (first.empty() || more == 0u) return first;
    return first + " and " + std::to_string(more) + " more";
}

} // namespace

const char *sex_name(Sex sex) { return sex == Sex::Female ? "female" : "male"; }

std::uint32_t armor_data(std::uint8_t kind) {
    switch (kind) {
    case 0: return kChestData;
    case 1: return kArmsData;
    case 2: return kWaistData;
    case 3: return kLegsData;
    case 4: return kHeadData;
    default: return 0u;
    }
}

std::optional<std::uint16_t> armor_model(const Ram &ram, std::uint8_t kind, std::uint16_t id, Sex sex) {
    const std::uint32_t data = armor_data(kind);
    const std::uint32_t at = data + static_cast<std::uint32_t>(id) * kArmorRecord;
    if (data == 0u || !ram.contains(at, kArmorRecord)) return std::nullopt;
    return ram.load16(at + (sex == Sex::Female ? 2u : 0u));
}

bool wearable_by(const Ram &ram, std::uint8_t kind, std::uint16_t id, Sex sex) {
    const std::uint32_t data = armor_data(kind);
    const std::uint32_t at = data + static_cast<std::uint32_t>(id) * kArmorRecord;
    if (data == 0u || !ram.contains(at, kArmorRecord)) return false;
    return (ram.load8(at + 4u) & (1u << static_cast<unsigned>(sex))) != 0u;
}

std::optional<Look> hunter_look(const Ram &ram) {
    if (!character_loaded(ram) || !ram.contains(character(kCharacterSex), 4u)) return std::nullopt;
    const std::uint8_t sex = ram.load8(character(kCharacterSex));
    if (sex > 1u) return std::nullopt;
    return Look{static_cast<Sex>(sex), ram.load8(character(kCharacterInnerWear))};
}

std::optional<Piece> worn_armor(const Ram &ram, std::uint8_t kind) {
    if (!is_armor(kind) || !character_loaded(ram)) return std::nullopt;
    // The records are in the order of the kinds: chest 0, arms 1, waist 2,
    // legs 3, head 4.
    const std::uint32_t at = character(kCharacterArmor) + static_cast<std::uint32_t>(kind) * kEquipmentRecord;
    if (!ram.contains(at, kEquipmentRecord)) return std::nullopt;
    if (ram.load8(at) == 0u) return Piece{kind, 0u};
    if (ram.load8(at + 1u) != kind) return std::nullopt;  // not the layout traced
    return Piece{kind, ram.load16(at + 2u)};
}

std::optional<Piece> carried_weapon(const Ram &ram) {
    if (!character_loaded(ram)) return std::nullopt;
    const std::uint32_t at = character(kCharacterWeapon);
    if (!ram.contains(at, kEquipmentRecord) || ram.load8(at) == 0u) return std::nullopt;
    const std::uint8_t kind = ram.load8(at + 1u);
    if (!is_weapon(kind)) return std::nullopt;
    return Piece{kind, ram.load16(at + 2u)};
}

std::optional<std::uint32_t> model_file(const Ram &ram, const Piece &piece, const Look &look) {
    if (is_armor(piece.kind)) {
        const auto files = armor_files(ram, look.sex, piece.kind);
        if (!files) return std::nullopt;
        std::optional<std::uint16_t> model =
            piece.id == 0u ? std::optional<std::uint16_t>{0} : armor_model(ram, piece.kind, piece.id, look.sex);
        // Model 0 is the bare part: the game shows the inner wear instead.
        if (model && *model == 0u) model = inner_wear_model(ram, piece.kind, look);
        if (!model || *model >= files->second) return std::nullopt;
        return files->first + *model;
    }
    const auto files = weapon_files(ram, piece.kind);
    const std::optional<std::uint16_t> model = weapon_model(ram, piece.kind, piece.id);
    if (!files || !model) return std::nullopt;
    return files->first + std::min<std::uint32_t>(*model, files->second - 1u);
}

std::optional<FileModel> file_model(const Ram &ram, std::uint32_t file) {
    for (const Sex sex : {Sex::Male, Sex::Female}) {
        for (std::uint8_t kind = 0; kind <= 4u; ++kind) {
            const auto files = armor_files(ram, sex, kind);
            if (files && file >= files->first && file < files->first + files->second)
                return FileModel{kind, sex, static_cast<std::uint16_t>(file - files->first)};
        }
    }
    for (const WeaponTable &t : kWeaponTables) {
        const auto files = weapon_files(ram, t.kind);
        if (files && file >= files->first && file < files->first + files->second)
            return FileModel{t.kind, std::nullopt, static_cast<std::uint16_t>(file - files->first)};
    }
    return std::nullopt;
}

std::string describe_file(const Ram &ram, std::uint32_t file) {
    const std::optional<FileModel> found = file_model(ram, file);
    if (!found) return {};
    const EquipmentKind *k = equipment_kind(found->kind);
    if (!found->sex) {
        const std::string names = pieces_with_model(ram, found->kind, std::nullopt, found->model);
        return names.empty() ? std::string(k != nullptr ? k->label : "Weapon") + " model " +
                                   std::to_string(found->model)
                             : names;
    }
    const std::string sex = sex_name(*found->sex);
    if (found->model == 0u) return "No armor, " + sex;
    // The inner wear's models, for an empty slot.
    for (std::uint8_t inner = 0; inner < kInnerWears; ++inner) {
        const std::optional<std::uint16_t> m = inner_wear_model(ram, found->kind, Look{*found->sex, inner});
        if (m && *m != 0u && *m == found->model)
            return "No armor (inner wear), " + sex;
    }
    const std::string names = pieces_with_model(ram, found->kind, found->sex, found->model);
    return (names.empty() ? "Armor model " + std::to_string(found->model) : names) + ", " + sex;
}

std::optional<std::uint8_t> kind_of_mod_part(std::string_view key) {
    struct Name {
        const char *key;
        std::uint8_t kind;
    };
    static constexpr Name kNames[] = {
        {"BODY", 0}, {"ARMS", 1}, {"WAIST", 2}, {"LEGS", 3}, {"HEAD", 4},  {"GS", 5},   {"SNS", 6},
        {"HMR", 7},  {"LNC", 8},  {"HBG", 9},   {"LBG", 11}, {"LS", 12},   {"SAXE", 13}, {"GL", 14},
        {"BOW", 15}, {"DB", 16},  {"HH", 17},
    };
    for (const Name &n : kNames) {
        const std::string_view name(n.key);
        if (name.size() == key.size() &&
            std::equal(name.begin(), name.end(), key.begin(), [](char a, char b) {
                return std::toupper(static_cast<unsigned char>(a)) == std::toupper(static_cast<unsigned char>(b));
            }))
            return n.kind;
    }
    return std::nullopt;
}

} // namespace mhp2g::game
