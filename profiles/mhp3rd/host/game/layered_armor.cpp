#include "game/layered_armor.hpp"

#include "game/equipment_models.hpp"
#include "game/game_data.hpp"
#include "game/guest_ram.hpp"

#include <set>

namespace mhp3rd::game::layered {

const char *part_label(std::uint8_t part) {
    static constexpr const char *kLabels[kParts] = {"Chest", "Arms", "Waist", "Legs", "Head"};
    return part < kParts ? kLabels[part] : "?";
}

std::optional<std::uint16_t> replacement_model(const Ram &ram, std::uint32_t block, std::uint32_t index,
                                               std::uint32_t part, const Pieces &pieces) {
    if (index != kOwnRecord || part >= kParts) return std::nullopt;
    const std::int32_t choice = pieces[part];
    if (choice < 0) return std::nullopt;
    // Only the block the game reaches through its pointer, as traced.
    if (!ram.contains(kGameBlockPointer, 4u) || ram.load32(kGameBlockPointer) != block) return std::nullopt;
    const std::uint32_t record = block + kHunterRecords + index * kHunterRecordBytes;
    if (!ram.contains(record, kHunterRecordBytes)) return std::nullopt;
    const Sex sex = (ram.load32(record + kRecordFlags) & 1u) != 0u ? Sex::Female : Sex::Male;
    const auto kind = static_cast<std::uint8_t>(part);
    const auto id = static_cast<std::uint16_t>(choice);
    // Nothing: the model the game gives an empty part, that of piece 0.
    if (id != 0u && !wearable_by(ram, kind, id, sex)) return std::nullopt;
    return armor_model(ram, kind, id, sex);
}

std::vector<Offer> offers(const Ram &ram, std::uint8_t part, bool all) {
    std::vector<Offer> list;
    const std::optional<Look> look = hunter_look(ram);
    if (part >= kParts || !look) return list;
    const std::vector<std::string> names = equipment_names(ram, part);
    const std::optional<Piece> worn = worn_armor(ram, part);
    std::set<std::uint16_t> owned;
    if (worn && worn->id != 0u) owned.insert(worn->id);
    for (std::uint32_t slot = 0; slot < kEquipmentBoxSlots; ++slot) {
        const std::uint32_t at = kEquipmentBox + slot * kEquipmentRecord;
        if (!ram.contains(at, kEquipmentRecord)) break;
        if (ram.load8(at) != 0u && ram.load8(at + 1u) == part) owned.insert(ram.load16(at + 2u));
    }
    for (std::size_t id = 1; id < names.size(); ++id) {
        const auto piece = static_cast<std::uint16_t>(id);
        const bool has = owned.contains(piece);
        if ((!all && !has) || names[id].empty() || !wearable_by(ram, part, piece, look->sex)) continue;
        list.push_back({piece, names[id], worn && worn->id == piece, has});
    }
    return list;
}

std::string choice_name(const Ram *ram, std::uint8_t part, std::int32_t choice) {
    if (choice < 0) return "Real equipment";
    if (choice == 0) return "Nothing";
    const auto id = static_cast<std::uint16_t>(choice);
    if (ram != nullptr) {
        const std::string name = equipment_name(*ram, part, id);
        if (!name.empty()) return name;
    }
    return "Piece " + std::to_string(id);
}

bool own_hunter(const Ram &ram, std::uint32_t object) {
    if (!ram.contains(object, kLoadPart + 4u)) return false;
    const std::uint32_t vtable = ram.load32(object);
    return ram.contains(vtable + kFileFunctionSlot, 4u) &&
           ram.load32(vtable + kFileFunctionSlot) == kRecordFileFunction &&
           ram.load16(object + kObjectIndex) == kOwnRecord;
}

bool load_done(const Ram &ram, std::uint32_t object) { return ram.load32(object + kLoadState) >= kLoadDone; }

void restart_load(Ram &ram, std::uint32_t object) {
    ram.store32(object + kLoadPart, 0u);
    ram.store32(object + kLoadState, 0u);
}

} // namespace mhp3rd::game::layered
