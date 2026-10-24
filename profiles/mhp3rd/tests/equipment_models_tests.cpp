// Which file holds a piece of equipment's model (game/equipment_models.hpp),
// run on a buffer standing for guest memory: the character's look and worn
// set, the armor and weapon tables, the inner wear for an empty slot, and the
// description of a file by the pieces that use it. No game data: the tables,
// file numbers and names here are made up.
#include "game/equipment_models.hpp"
#include "game/game_data.hpp"
#include "game/guest_ram.hpp"
#include "game/layered_armor.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace mhp3rd::game;
int failures{};

void check(bool condition, const char *message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

constexpr std::uint32_t kBase = 0x08800000u;
constexpr std::size_t kSize = 0x02000000u; // up to 0x0A800000

void write_table(Ram &ram, std::uint32_t at, const std::vector<std::string> &strings) {
    const auto count = static_cast<std::uint32_t>(strings.size());
    std::uint32_t text = (count + 1u) * 4u;
    for (std::uint32_t i = 0; i < count; ++i) {
        ram.store32(at + i * 4u, text);
        for (std::size_t c = 0; c <= strings[i].size(); ++c)
            ram.store8(at + text + static_cast<std::uint32_t>(c),
                c < strings[i].size() ? static_cast<std::uint8_t>(strings[i][c]) : 0u);
        text += static_cast<std::uint32_t>(strings[i].size() + 1u);
    }
    ram.store32(at + count * 4u, 0xFFFFFFFFu);
}

void write_text(Ram &ram) {
    const auto table = [&ram](int index, std::uint32_t at, const std::vector<std::string> &names) {
        ram.store32(kTextBlock + static_cast<std::uint32_t>(index) * 4u, at - kTextBlock);
        write_table(ram, at, names);
    };
    table(31, kTextBlock + 0x1000u, {"No Equipment", "Plain Mail", "Test Suit", "Plain Mail S"});
    table(29, kTextBlock + 0x2000u, {"No Equipment", "Cap"});
    table(33, kTextBlock + 0x3000u, {"No Equipment"});
    table(5, kTextBlock + 0x4000u, {"No Equipment", "Stick", "Big Stick"});
}

// An armor record: male and female model, who can wear it.
void write_armor(
    Ram &ram, std::uint32_t table, std::uint16_t id, std::uint16_t male, std::uint16_t female, std::uint8_t who) {
    const std::uint32_t at = table + id * kArmorRecord;
    ram.store16(at, male);
    ram.store16(at + 2u, female);
    ram.store8(at + 4u, who);
}

void write_tables(Ram &ram) {
    // The first file of each part (chest, arms, waist, legs, head, hair, face).
    const std::uint16_t male[kParts] = {100, 200, 300, 400, 500, 600, 700};
    const std::uint16_t female[kParts] = {1000, 1100, 1200, 1300, 1400, 1500, 1600};
    for (std::uint32_t p = 0; p < kParts; ++p) {
        ram.store16(kArmorFileBase + p * 2u, male[p]);
        ram.store16(kArmorFileBase + (kParts + p) * 2u, female[p]);
    }
    // The inner wear: one list for both sexes, as the game has it.
    const std::uint32_t list = 0x08A00000u;
    for (std::uint16_t i = 0; i < 4u; ++i) ram.store16(list + i * 2u, static_cast<std::uint16_t>(40u + i));
    for (const std::uint32_t table : {kChestInnerWear, kArmsInnerWear, kLegsInnerWear}) {
        ram.store32(table, list);
        ram.store32(table + 4u, list);
    }
    write_armor(ram, kChestData, 1, 3, 4, 0x0F);
    write_armor(ram, kChestData, 2, 5, 0, 0x05); // for men only
    write_armor(ram, kChestData, 3, 3, 4, 0x0F); // the same look as 1
    write_armor(ram, kHeadData, 1, 7, 7, 0x0F);
    // Great swords: the first file and how many models (every 4 bytes).
    ram.store16(kWeaponFileBase, 2000u);
    ram.store16(kWeaponModelCount, 10u);
    ram.store16(0x08997AA0u + 1u * 28u, 3u);
    ram.store16(0x08997AA0u + 2u * 28u, 50u); // past the count: the game uses the last
}

// The character: a fullwidth name, sex, inner wear, weapon and armor.
void write_character(Ram &ram, std::uint8_t sex, std::uint8_t inner) {
    ram.store16(kCharacter, 0xFF34u); // "T"
    ram.store8(kCharacter + kCharacterSex, sex);
    ram.store8(kCharacter + kCharacterInnerWear, inner);
    const auto record = [&ram](std::uint32_t at, std::uint8_t kind, std::uint16_t id) {
        ram.store8(at, 1u);
        ram.store8(at + 1u, kind);
        ram.store16(at + 2u, id);
    };
    record(kCharacter + kCharacterWeapon, 5, 1);
    record(kCharacter + kCharacterArmor, 0, 1);                         // chest
    record(kCharacter + kCharacterArmor + 4u * kEquipmentRecord, 4, 1); // head
}

void test_nothing_loaded() {
    BufferRam ram(kBase, kSize);
    write_text(ram);
    write_tables(ram);
    check(!hunter_look(ram), "no look before a character is loaded");
    check(!worn_armor(ram, 0), "no armor before a character is loaded");
    check(!carried_weapon(ram), "no weapon before a character is loaded");
    // The tables alone still describe files.
    check(describe_file(ram, 1004) == "Plain Mail and 1 more, female", "a file shared by two pieces");
}

void test_files() {
    BufferRam ram(kBase, kSize);
    write_text(ram);
    write_tables(ram);
    write_character(ram, 1, 2);
    const std::optional<Look> look = hunter_look(ram);
    check(look && look->sex == Sex::Female && look->inner_wear == 2, "the look: sex and inner wear");
    if (!look) return;
    const std::optional<Piece> chest = worn_armor(ram, 0);
    const std::optional<Piece> arms = worn_armor(ram, 1);
    check(chest && chest->id == 1 && arms && arms->id == 0, "the worn armor, and an empty slot");
    check(model_file(ram, *chest, *look) == 1004u, "a chest for a woman: her model");
    check(model_file(ram, *chest, Look{Sex::Male, 0}) == 103u, "the same chest for a man");
    check(model_file(ram, *arms, *look) == 1142u, "empty arms show the inner wear");
    check(model_file(ram, Piece{2, 0}, *look) == 1200u, "an empty waist is the bare part");
    check(model_file(ram, Piece{4, 1}, *look) == 1407u, "a head");
    check(model_file(ram, Piece{0, 2}, *look) == 1042u, "a piece with no model for her shows the inner wear");
    const std::optional<Piece> weapon = carried_weapon(ram);
    check(weapon && weapon->kind == 5 && weapon->id == 1, "the carried weapon");
    if (weapon) check(model_file(ram, *weapon, *look) == 2003u, "a weapon's file");
    check(model_file(ram, Piece{5, 2}, *look) == 2009u, "a weapon's model is kept below the count");

    const std::optional<FileModel> found = file_model(ram, 1004);
    check(found && found->kind == 0 && found->sex == Sex::Female && found->model == 4, "whose model a file is");
    check(!file_model(ram, 5), "other files are nobody's");
    check(describe_file(ram, 105) == "Test Suit, male", "a piece for men only");
    check(describe_file(ram, 1142) == "No armor (inner wear), female", "the inner wear");
    check(describe_file(ram, 1200) == "No armor, female", "a bare part");
    check(describe_file(ram, 1407) == "Cap, female", "a head");
    check(describe_file(ram, 2003) == "Stick", "a weapon");
    check(describe_file(ram, 5).empty(), "other files have no description");
}

void test_mod_parts() {
    check(kind_of_mod_part("HEAD") == 4, "HEAD is the head");
    check(kind_of_mod_part("body") == 0, "keys ignore case");
    check(kind_of_mod_part("SAXE") == 13, "a weapon class");
    check(!kind_of_mod_part("CATHELM"), "Felyne gear is not the hunter's");
}

// Layered armor (game/layered_armor.hpp): the model it gives the game's lookup
// for the hunter played here, and what it leaves to the game.
void test_layered_armor() {
    namespace layered = mhp3rd::game::layered;
    BufferRam ram(kBase, kSize);
    write_text(ram);
    write_tables(ram);
    write_character(ram, 1, 2);
    // The block with the hunter records, and two hunters: this one (female)
    // and another player (male).
    const std::uint32_t block = 0x09000000u;
    ram.store32(layered::kGameBlockPointer, block);
    const std::uint32_t own = block + layered::kHunterRecords;
    const std::uint32_t other = own + layered::kHunterRecordBytes;
    ram.store32(own + layered::kRecordFlags, 1u);
    ram.store32(other + layered::kRecordFlags, 0u);

    layered::Pieces pieces{layered::kReal, layered::kReal, layered::kReal, layered::kReal, layered::kReal};
    check(!layered::replacement_model(ram, block, 0, 0, pieces), "a real part is left to the game");
    pieces[0] = 1; // chest: Plain Mail, models 3 and 4
    check(layered::replacement_model(ram, block, 0, 0, pieces) == std::optional<std::uint16_t>{4},
        "the chosen chest's model for her");
    check(!layered::replacement_model(ram, block, 1, 0, pieces), "another player's hunter is left to the game");
    check(!layered::replacement_model(ram, block + 4u, 0, 0, pieces), "a block not the game's is left alone");
    check(!layered::replacement_model(ram, block, 0, 5, pieces), "hair and face are left to the game");
    ram.store32(own + layered::kRecordFlags, 0u);
    check(layered::replacement_model(ram, block, 0, 0, pieces) == std::optional<std::uint16_t>{3},
        "the record's sex picks the model");
    ram.store32(own + layered::kRecordFlags, 1u);
    pieces[0] = 2; // for men only
    check(!layered::replacement_model(ram, block, 0, 0, pieces), "a piece she cannot wear shows the real one");
    pieces[0] = 0;
    check(layered::replacement_model(ram, block, 0, 0, pieces) == std::optional<std::uint16_t>{0},
        "nothing: the model of an empty part");

    // Offers: the pieces owned, or all she can wear.
    ram.store8(kEquipmentBox, 1u);
    ram.store8(kEquipmentBox + 1u, 0u);
    ram.store16(kEquipmentBox + 2u, 3u);
    const std::vector<layered::Offer> owned = layered::offers(ram, 0, false);
    check(owned.size() == 2u && owned[0].id == 1 && owned[0].worn && owned[1].id == 3 && !owned[1].worn,
        "owned chests: the one worn and the one in the box");
    const std::vector<layered::Offer> all = layered::offers(ram, 0, true);
    check(all.size() == 2u, "all chests she can wear, not the one for men");
    check(!all.empty() && all[0].name == "Plain Mail", "offers carry the game's names");
    check(layered::choice_name(&ram, 4, 1) == "Cap", "a choice named by the game");
    check(layered::choice_name(nullptr, 4, 1) == "Piece 1", "a choice by id without the game");
    check(layered::choice_name(&ram, 4, layered::kReal) == "Real equipment", "the real piece");

    // The hunter's load: only the hunter played here, drawn from its record.
    const std::uint32_t hunter = 0x09100000u;
    const std::uint32_t vtable = 0x09200000u;
    ram.store32(hunter, vtable);
    ram.store32(vtable + layered::kFileFunctionSlot, layered::kRecordFileFunction);
    ram.store16(hunter + layered::kObjectIndex, 0u);
    ram.store32(hunter + layered::kLoadState, 4u);
    ram.store32(hunter + layered::kLoadPart, 9u);
    check(layered::own_hunter(ram, hunter) && layered::load_done(ram, hunter), "the hunter played here, loaded");
    layered::restart_load(ram, hunter);
    check(ram.load32(hunter + layered::kLoadState) == 0u && ram.load32(hunter + layered::kLoadPart) == 0u,
        "a restarted load begins at the first part");
    check(!layered::load_done(ram, hunter), "and is running");
    ram.store16(hunter + layered::kObjectIndex, 1u);
    check(!layered::own_hunter(ram, hunter), "another player's hunter is not ours");
}

} // namespace

int main() {
    test_layered_armor();
    test_nothing_loaded();
    test_files();
    test_mod_parts();
    if (failures == 0) std::cout << "equipment models tests passed\n";
    return failures == 0 ? 0 : 1;
}
