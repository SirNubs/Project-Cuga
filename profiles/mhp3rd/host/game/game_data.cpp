#include "game/game_data.hpp"

#include "game/guest_ram.hpp"

namespace mhp3rd::game {
namespace {

constexpr std::uint32_t kTableEnd = 0xFFFFFFFFu;
// More entries than any of the game's tables has; a larger count means the
// bytes are not a table.
constexpr std::uint32_t kMostEntries = 4096u;
constexpr std::size_t kLongestText = 256u;

// Where table `index` of the text block starts and how many entries it has,
// or nothing when it does not look like a table.
std::optional<std::pair<std::uint32_t, std::uint32_t>> find_table(const Ram &ram, std::uint32_t block, int index) {
    const std::uint32_t entry = block + static_cast<std::uint32_t>(index) * 4u;
    if (index < 0 || !ram.contains(entry, 4u)) return std::nullopt;
    const std::uint32_t table = block + read32(ram, entry);
    if (!ram.contains(table, 8u)) return std::nullopt;
    // The first offset points just past the list and its end marker.
    const std::uint32_t first = read32(ram, table);
    if (first < 8u || first % 4u != 0u || first / 4u - 1u > kMostEntries) return std::nullopt;
    const std::uint32_t count = first / 4u - 1u;
    if (!ram.contains(table, first) || read32(ram, table + count * 4u) != kTableEnd) return std::nullopt;
    return std::make_pair(table, count);
}

} // namespace

std::uint32_t read32(const Ram &ram, std::uint32_t address) {
    return static_cast<std::uint32_t>(ram.load8(address)) | (static_cast<std::uint32_t>(ram.load8(address + 1u)) << 8u) |
           (static_cast<std::uint32_t>(ram.load8(address + 2u)) << 16u) |
           (static_cast<std::uint32_t>(ram.load8(address + 3u)) << 24u);
}

std::string read_text(const Ram &ram, std::uint32_t address) {
    std::string text;
    for (std::size_t i = 0; i < kLongestText && ram.contains(address + static_cast<std::uint32_t>(i), 1u); ++i) {
        const char c = static_cast<char>(ram.load8(address + static_cast<std::uint32_t>(i)));
        if (c == '\0') break;
        text += c;
    }
    return text;
}

bool character_loaded(const Ram &ram) { return !hunter_name(ram).empty(); }

std::string hunter_name(const Ram &ram) {
    std::string name;
    if (!ram.contains(kHunterName, kHunterNameUnits * 2u)) return name;
    for (std::size_t i = 0; i < kHunterNameUnits; ++i) {
        std::uint32_t c = ram.load16(kHunterName + static_cast<std::uint32_t>(i * 2u));
        if (c == 0u) break;
        // The game keeps ASCII names as their fullwidth forms.
        if (c >= 0xFF01u && c <= 0xFF5Eu) c -= 0xFEE0u;
        if (c == 0x3000u) c = ' ';
        if (c < 0x80u) {
            name += static_cast<char>(c);
        } else if (c < 0x800u) {
            name += static_cast<char>(0xC0u | (c >> 6u));
            name += static_cast<char>(0x80u | (c & 0x3Fu));
        } else {
            name += static_cast<char>(0xE0u | (c >> 12u));
            name += static_cast<char>(0x80u | ((c >> 6u) & 0x3Fu));
            name += static_cast<char>(0x80u | (c & 0x3Fu));
        }
    }
    return name;
}

std::vector<std::string> text_table(const Ram &ram, std::uint32_t block, int index) {
    std::vector<std::string> out;
    const auto table = find_table(ram, block, index);
    if (!table) return out;
    out.reserve(table->second);
    for (std::uint32_t i = 0; i < table->second; ++i)
        out.push_back(read_text(ram, table->first + read32(ram, table->first + i * 4u)));
    return out;
}

std::string text_entry(const Ram &ram, std::uint32_t block, int index, std::uint32_t entry) {
    const auto table = find_table(ram, block, index);
    if (!table || entry >= table->second) return {};
    return read_text(ram, table->first + read32(ram, table->first + entry * 4u));
}

std::uint32_t text_count(const Ram &ram, std::uint32_t block, int index) {
    const auto table = find_table(ram, block, index);
    return table ? table->second : 0u;
}

const std::vector<EquipmentKind> &equipment_kinds() {
    // Kinds and tables as traced: the equipment box sorted by the game lists
    // the weapon kinds in the order below, and each kind's ids fit only its
    // own name table.
    static const std::vector<EquipmentKind> kinds{
        {4, 29, "Head"},          {0, 31, "Chest"},         {1, 33, "Arms"},       {2, 35, "Waist"},
        {3, 37, "Legs"},          {5, 5, "Great Sword"},    {12, 17, "Long Sword"}, {6, 7, "Sword and Shield"},
        {16, 25, "Dual Blades"},  {7, 9, "Hammer"},         {17, 27, "Hunting Horn"}, {8, 11, "Lance"},
        {14, 21, "Gunlance"},     {13, 19, "Switch Axe"},   {11, 15, "Light Bowgun"}, {9, 13, "Heavy Bowgun"},
        {15, 23, "Bow"},
    };
    return kinds;
}

const EquipmentKind *equipment_kind(std::uint8_t kind) {
    for (const EquipmentKind &k : equipment_kinds())
        if (k.kind == kind) return &k;
    return nullptr;
}

bool is_armor(std::uint8_t kind) { return kind <= 4u; }

bool is_weapon(std::uint8_t kind) { return kind >= 5u && equipment_kind(kind) != nullptr; }

std::vector<std::string> equipment_names(const Ram &ram, std::uint8_t kind) {
    const EquipmentKind *k = equipment_kind(kind);
    if (k == nullptr) return {};
    return text_table(ram, kTextBlock, k->name_table);
}

std::string equipment_name(const Ram &ram, std::uint8_t kind, std::uint16_t id) {
    const EquipmentKind *k = equipment_kind(kind);
    if (k == nullptr) return {};
    return text_entry(ram, kTextBlock, k->name_table, id);
}

} // namespace mhp3rd::game
