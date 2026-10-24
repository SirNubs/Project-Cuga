#include "game/key_quest_archive.hpp"
#include "mods/mhp2g_data_bin.hpp"
#include "mods/disc_read_overlay.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

void check(bool value, const char *message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
std::vector<std::uint8_t> fixture(bool key, unsigned padding = 3u) {
    std::vector<std::uint8_t> b(8u + 0x34u);
    b[0] = 8; b[8] = 4; b[11] = key ? 4 : 0;
    const char title[] = "Test quest";
    b.insert(b.end(), title, title + 10);
    b.insert(b.end(), padding, 0);
    const char objective[] = "Unchanged objective";
    b.insert(b.end(), objective, objective + sizeof(objective));
    return b;
}
int main(int argc, char **argv) {
    using namespace mhp2g;
    auto fuc = game::key_quest_files(6602, true);
    auto us = game::key_quest_files(6657, false);
    check(fuc && fuc->first == 5310 && fuc->last == 5320, "FUC range retained");
    check(us && us->first == 5342 && us->last == 5352, "US range selected");
    check(game::key_quest_files(6657, true).has_value(), "repacked US layout");
    check(!game::key_quest_files(6602, false) && !game::key_quest_files(6656, false), "unknown layouts rejected");
    for (bool plain : {false, true}) {
        mods::p3rd::set_plain_archive(plain);
        auto original = fixture(true);
        auto stored = original;
        mods::p3rd::encrypt(stored, 12345, 0);
        check(game::mark_stored_key_quests(stored, 12345) == 1, "stored key quest marked");
        auto once = stored;
        check(game::mark_stored_key_quests(stored, 12345) == 0 && stored == once, "stored transform idempotent");
        mods::p3rd::decrypt(stored, 12345, 0);
        check(stored.size() == original.size() && stored[60] == '*', "size and star");
        check(std::equal(original.begin(), original.begin()+60, stored.begin()), "record header unchanged");
        check(std::equal(original.begin()+72, original.end(), stored.begin()+72), "objective unchanged");
        for (auto invalid : {fixture(false), fixture(true, 1), std::vector<std::uint8_t>{1,2,3}}) {
            mods::p3rd::encrypt(invalid, 5678, 0);
            auto before = invalid;
            check(game::mark_stored_key_quests(invalid, 5678) == 0 && invalid == before, "invalid/nonkey stored bytes unchanged");
        }
    }
    unsigned calls = 0;
    auto read = [&](std::uint64_t offset, std::span<std::uint8_t> bytes) {
        ++calls;
        for (std::size_t i=0; i<bytes.size(); ++i) bytes[i]=static_cast<std::uint8_t>(128+offset+i);
        return bytes.size();
    };
    std::vector<std::uint8_t> raw(40, 7);
    check(mods::overlay_archive_read(90, std::span(raw), 100, 20, read) == 40, "straddling UMD read length");
    check(calls == 1 && raw[9]==7 && raw[10]==128 && raw[29]==147 && raw[30]==7, "only DATA.BIN span replaced");
    raw.assign(4,7);
    check(mods::overlay_archive_read(105, std::span(raw), 100, 20, read) == 4 && raw[0]==133 && raw[3]==136, "interior partial read offset");
    const auto before=calls;
    raw.assign(4,7);
    check(mods::overlay_archive_read(96, std::span(raw), 100, 20, read)==4 && calls==before && raw[0]==7, "read ending at archive unchanged");
    check(mods::overlay_archive_read(120, std::span(raw), 100, 20, read)==4 && calls==before, "read after archive unchanged");
    check(mods::overlay_archive_read(100, std::span(raw), 100, 0, read)==4 && calls==before, "empty archive unchanged");
    check(mods::overlay_archive_read(98, std::span(raw), 100, 20,
        [](std::uint64_t, std::span<std::uint8_t>) { return std::size_t(1); })==3, "short archive read propagated");
    if (argc == 5) {
        std::ifstream input(argv[1], std::ios::binary);
        check(input.good(), "real archive entry readable");
        std::vector<std::uint8_t> stored((std::istreambuf_iterator<char>(input)), {});
        mods::p3rd::set_plain_archive(false);
        const auto block = static_cast<std::uint32_t>(std::stoul(argv[2]));
        check(game::mark_stored_key_quests(stored, block) == std::stoul(argv[3]), "real US key count");
        std::ofstream output(argv[4], std::ios::binary);
        output.write(reinterpret_cast<const char *>(stored.data()), stored.size());
        check(output.good(), "real transformed entry written");
    }
    std::cout << "US/FUC quest archive tests passed\n";
}
