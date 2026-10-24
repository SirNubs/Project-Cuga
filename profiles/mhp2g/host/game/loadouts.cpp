#include "game/loadouts.hpp"

#include "camera/lock_on.hpp"
#include "install/user_data.hpp"
#include "psprecomp/guest_memory.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace mhp2g::game {
namespace {

// The player's data (camera/lock_on.hpp, restock): pouch and item box are (u16 item, s16 count) slots.
constexpr std::uint32_t kPlayerData = 0x089CC558u;
constexpr std::uint32_t kPouch = 0x6AEA6u;
constexpr std::uint32_t kBox = 0x3428u;
constexpr std::uint32_t kBoxSlots = 1000u;
constexpr std::uint32_t kItemTable = 0x0899D2A8u;
constexpr std::uint32_t kItemSize = 24u;
constexpr std::uint32_t kItems = 1400u;
constexpr int kBoxStack = 99; // the most one slot of the box holds

struct State {
    std::array<Loadout, kLoadouts> sets;
    bool read{};
    std::atomic<int> save{-1};
    std::atomic<int> load{-1};
};
State &state() {
    static State value;
    return value;
}

std::filesystem::path file() {
    return install::user_data_directory() / "loadouts.txt";
}

// One line a loadout: name, a tab, 1 or 0 (used), a tab, "item:count" pairs with commas.
void read() {
    State &s = state();
    if (s.read) return;
    s.read = true;
    for (int i = 0; i < kLoadouts; ++i) s.sets[static_cast<std::size_t>(i)].name = "Loadout " + std::to_string(i + 1);
    std::ifstream in(file());
    std::string line;
    for (int i = 0; i < kLoadouts && std::getline(in, line); ++i) {
        Loadout &set = s.sets[static_cast<std::size_t>(i)];
        const std::size_t a = line.find('\t');
        const std::size_t b = a == std::string::npos ? a : line.find('\t', a + 1u);
        if (b == std::string::npos) continue;
        if (a != 0u) set.name = line.substr(0u, a);
        set.used = line.substr(a + 1u, b - a - 1u) == "1";
        std::stringstream pairs(line.substr(b + 1u));
        std::string pair;
        for (std::size_t slot = 0u; slot < set.items.size() && std::getline(pairs, pair, ',');) {
            const std::size_t colon = pair.find(':');
            if (colon == std::string::npos) continue;
            const unsigned long item = std::strtoul(pair.c_str(), nullptr, 10);
            const unsigned long count = std::strtoul(pair.c_str() + colon + 1u, nullptr, 10);
            if (item == 0u || item >= kItems || count == 0u || count > 99u) continue;
            set.items[slot++] = {static_cast<std::uint16_t>(item), static_cast<std::uint16_t>(count)};
        }
    }
}

void write() {
    std::ofstream out(file(), std::ios::trunc);
    for (const Loadout &set : state().sets) {
        out << set.name << '\t' << (set.used ? 1 : 0) << '\t';
        bool first = true;
        for (const auto &[item, count] : set.items) {
            if (item == 0u) continue;
            out << (first ? "" : ",") << item << ':' << count;
            first = false;
        }
        out << '\n';
    }
}

// Where the player's data is, or 0 when it is not all there.
std::uint32_t player_data(const psprecomp::GuestMemory &memory) {
    const std::uint32_t data = memory.raw_pointer(kPlayerData, 4u) != nullptr ? memory.load32(kPlayerData) : 0u;
    if (data == 0u || memory.raw_pointer(data + kBox, kBoxSlots * 4u) == nullptr ||
        memory.raw_pointer(data + kPouch, kPouchSlots * 4u) == nullptr ||
        memory.raw_pointer(kItemTable, kItems * kItemSize) == nullptr)
        return 0u;
    return data;
}

// Puts `count` of an item into the box: onto its stacks first, then into free slots. Returns what did not fit.
int into_box(psprecomp::GuestMemory &memory, std::uint32_t data, std::uint32_t item, int count) {
    for (int pass = 0; pass < 2 && count > 0; ++pass) {
        for (std::uint32_t b = 0u; b < kBoxSlots && count > 0; ++b) {
            const std::uint32_t at = data + kBox + b * 4u;
            const std::uint32_t held = memory.load16(at);
            const int stored = static_cast<std::int16_t>(memory.load16(at + 2u));
            if (pass == 0 ? (held != item || stored <= 0 || stored >= kBoxStack) : (held != 0u && stored > 0)) continue;
            const int room = pass == 0 ? kBoxStack - stored : kBoxStack;
            const int put = std::min(room, count);
            memory.store16(at, static_cast<std::uint16_t>(item));
            memory.store16(at + 2u, static_cast<std::uint16_t>((pass == 0 ? stored : 0) + put));
            count -= put;
        }
    }
    return count;
}

// Takes up to `count` of an item out of the box. Returns how many came.
int out_of_box(psprecomp::GuestMemory &memory, std::uint32_t data, std::uint32_t item, int count) {
    int got = 0;
    for (std::uint32_t b = 0u; b < kBoxSlots && got < count; ++b) {
        const std::uint32_t at = data + kBox + b * 4u;
        if (memory.load16(at) != item) continue;
        const int stored = static_cast<std::int16_t>(memory.load16(at + 2u));
        if (stored <= 0) continue;
        const int take = std::min(count - got, stored);
        memory.store16(at + 2u, static_cast<std::uint16_t>(stored - take));
        if (stored == take) memory.store16(at, 0u);
        got += take;
    }
    return got;
}

std::string save(psprecomp::GuestMemory &memory, int index) {
    const std::uint32_t data = player_data(memory);
    if (data == 0u) return {};
    Loadout &set = state().sets[static_cast<std::size_t>(index)];
    set.items = {};
    std::size_t slot = 0u;
    for (std::uint32_t p = 0u; p < static_cast<std::uint32_t>(kPouchSlots); ++p) {
        const std::uint32_t item = memory.load16(data + kPouch + p * 4u);
        const int count = static_cast<std::int16_t>(memory.load16(data + kPouch + p * 4u + 2u));
        if (item == 0u || item >= kItems || count <= 0) continue;
        set.items[slot++] = {static_cast<std::uint16_t>(item), static_cast<std::uint16_t>(std::min(count, 99))};
    }
    set.used = true;
    write();
    return "Saved " + set.name + ": " + std::to_string(set.kinds()) + (set.kinds() == 1 ? " kind of item" : " kinds of item");
}

std::string load(psprecomp::GuestMemory &memory, int index) {
    const std::uint32_t data = player_data(memory);
    if (data == 0u) return {};
    const Loadout &set = state().sets[static_cast<std::size_t>(index)];
    if (!set.used) return set.name + " is empty: save it first";
    // The pouch goes back into the box. What the box has no room for stays in the pouch, and nothing is loaded.
    for (std::uint32_t p = 0u; p < static_cast<std::uint32_t>(kPouchSlots); ++p) {
        const std::uint32_t at = data + kPouch + p * 4u;
        const std::uint32_t item = memory.load16(at);
        const int count = static_cast<std::int16_t>(memory.load16(at + 2u));
        if (item == 0u || count <= 0) {
            memory.store32(at, 0u);
            continue;
        }
        const int left = into_box(memory, data, item, count);
        if (left > 0) {
            memory.store16(at + 2u, static_cast<std::uint16_t>(left));
            return "The item box is full: " + set.name + " was not loaded";
        }
        memory.store32(at, 0u);
    }
    std::uint32_t slot = 0u;
    std::vector<std::uint32_t> short_of;
    for (const auto &[item, wanted] : set.items) {
        if (item == 0u) continue;
        const int most = memory.load8(kItemTable + item * kItemSize + 19u);
        const int want = most > 0 ? std::min<int>(wanted, most) : wanted;
        const int got = out_of_box(memory, data, item, want);
        if (got < want) short_of.push_back(item);
        if (got <= 0) continue;
        memory.store16(data + kPouch + slot * 4u, item);
        memory.store16(data + kPouch + slot * 4u + 2u, static_cast<std::uint16_t>(got));
        ++slot;
    }
    return "Loaded " + set.name + (short_of.empty() ? "" : ". Missing: " + item_names(memory, short_of));
}

} // namespace

std::string item_name(const psprecomp::GuestMemory &memory, std::uint32_t item) {
    constexpr std::uint32_t kNames = 0x089D65ADu;
    constexpr std::uint32_t kNamesEnd = kNames + 0x8000u;
    const std::string fallback = "item " + std::to_string(item);
    // The list starts with the nameless item 0: four 0xFF bytes and dashes.
    if (item == 0u || item >= kItems || memory.raw_pointer(kNames, kNamesEnd - kNames) == nullptr ||
        memory.load8(kNames) != 0xFFu || memory.load8(kNames + 3u) != 0xFFu)
        return fallback;
    std::uint32_t at = kNames;
    for (std::uint32_t n = 0u; n < item; ++n) {
        while (at < kNamesEnd && memory.load8(at) != 0u) ++at;
        ++at;
    }
    std::string name;
    for (; at < kNamesEnd && name.size() < 24u; ++at) {
        const std::uint8_t c = memory.load8(at);
        if (c == 0u) break;
        if (c < 0x20u || c > 0x7Eu) return fallback; // not plain text: not the list
        name += static_cast<char>(c);
    }
    return name.empty() ? fallback : name;
}

std::string item_names(const psprecomp::GuestMemory &memory, const std::vector<std::uint32_t> &items) {
    std::string out;
    for (std::size_t i = 0u; i < items.size() && i < 3u; ++i) out += (i != 0u ? ", " : "") + item_name(memory, items[i]);
    if (items.size() > 3u) out += " and " + std::to_string(items.size() - 3u) + " more";
    return out;
}

int Loadout::kinds() const {
    return static_cast<int>(std::count_if(items.begin(), items.end(), [](const auto &entry) { return entry.first != 0u; }));
}

const Loadout &loadout(int index) {
    read();
    return state().sets[static_cast<std::size_t>(std::clamp(index, 0, kLoadouts - 1))];
}

void rename_loadout(int index, const std::string &name) {
    read();
    if (index < 0 || index >= kLoadouts) return;
    std::string clean;
    for (const char c : name)
        if (c != '\t' && c != '\n' && c != '\r') clean += c;
    if (clean.empty()) return;
    state().sets[static_cast<std::size_t>(index)].name = clean;
    write();
}

void request_save_loadout(int index) {
    if (index >= 0 && index < kLoadouts) state().save = index;
}

void request_load_loadout(int index) {
    if (index >= 0 && index < kLoadouts) state().load = index;
}

std::string loadouts_frame(psprecomp::GuestMemory &memory) {
    read();
    const int save_index = state().save.exchange(-1);
    const int load_index = state().load.exchange(-1);
    if (save_index < 0 && load_index < 0) return {};
    // With the box's own first menu up only: the game then holds no working copy of the pouch or the box.
    static const bool trace = std::getenv("MHP2G_TRACE_LOADOUTS") != nullptr;
    if (trace)
        std::cout << "[loadouts] save " << save_index << " load " << load_index << " box menu "
                  << camera::restock_offered(memory) << " data " << std::hex << player_data(memory) << std::dec << std::endl;
    if (!camera::restock_offered(memory)) return {};
    if (save_index >= 0) return save(memory, save_index);
    return load(memory, load_index);
}

} // namespace mhp2g::game
