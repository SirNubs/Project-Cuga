#include "debug/debug_tools.hpp"

#include "debug/debug_console.hpp"
#include "debug/game_state.hpp"
#include "debug/quest_start.hpp"
#include "game/guest_ram.hpp"
#include "mods/mhp2g_data_bin.hpp"
#include "mods/mhp2g_mods.hpp"

#include "adhoc/session.hpp"
#include "hle/hle_common.hpp"

#include "psprecomp/guest_memory.hpp"
#include "psprecomp/runtime.hpp"

#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <optional>
#include <utility>

namespace mhp2g::debug {

using game::GuestRam;

namespace {

constexpr std::size_t kRecentLines = 6u;

std::deque<std::string> &recent_lines() {
    static std::deque<std::string> lines;
    return lines;
}

struct Request {
    std::string what;
    std::function<std::string(Ram &)> change;
};

struct State {
    psprecomp::Runtime *runtime{};
    std::deque<Request> pending;
    HeldCheats held;
    std::vector<std::string> quest;
};

State &state() {
    static State s;
    return s;
}

void run_pending(Ram &ram) {
    State &s = state();
    while (!s.pending.empty()) {
        Request r = std::move(s.pending.front());
        s.pending.pop_front();
        if (const std::string why = blocked_reason(); !why.empty()) {
            log("refused (" + why + "): " + r.what);
            continue;
        }
        log(r.change(ram));
    }
}

// A DATA.BIN entry's plain bytes, as the game reads the archive (with a mod's
// files in it when mods are on). The directory is read again each time: mods
// can move entries while the game runs.
std::optional<std::vector<std::uint8_t>> read_entry(std::uint32_t entry) {
    namespace db = mods::p3rd;
    const std::uint64_t archive = mods::data_bin_size();
    std::vector<std::uint8_t> head(db::kBlock);
    head.resize(mods::read_data_bin(0u, head));
    if (head.size() < 4u) return std::nullopt;
    std::vector<std::uint8_t> first(head.begin(), head.begin() + 4);
    db::decrypt(first, 0u, 0u);
    const std::uint32_t blocks =
        first[0] | first[1] << 8u | first[2] << 16u | static_cast<std::uint32_t>(first[3]) << 24u;
    if (blocks == 0u || blocks >= 512u) return std::nullopt;
    std::vector<std::uint8_t> encrypted(static_cast<std::size_t>(blocks) * db::kBlock);
    encrypted.resize(mods::read_data_bin(0u, encrypted));
    const std::optional<db::Directory> directory = db::Directory::parse(encrypted, archive);
    if (!directory || entry >= directory->entries()) return std::nullopt;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(directory->size(entry)));
    const std::uint32_t block = directory->blocks[entry];
    bytes.resize(mods::read_data_bin(static_cast<std::uint64_t>(block) * db::kBlock, bytes));
    if (!db::verbatim_magic(bytes)) db::decrypt(bytes, block, 0u);
    return bytes;
}

} // namespace

bool enabled() {
    static const bool on = [] {
        const char *text = std::getenv("MHP2G_DEBUG_MENU");
        const bool value = text != nullptr && std::strcmp(text, "1") == 0;
        if (value) std::cout << "[debug] developer tools on (MHP2G_DEBUG_MENU=1): Debug page in the menu" << std::endl;
        return value;
    }();
    return on;
}

std::string blocked_reason() {
    if (adhoc_session_active() || adhoc_networking_on()) return "ad hoc play is on";
    return {};
}

void request(std::string what, std::function<std::string(Ram &)> change) {
    if (!enabled()) return;
    state().pending.push_back({std::move(what), std::move(change)});
    // The menu runs between game frames; when it pauses the game there is no
    // flip until it closes, so run the request here, still between frames.
    if (psprecomp::Runtime *rt = state().runtime) {
        GuestRam ram(rt->memory());
        run_pending(ram);
    }
}

void read(const std::function<void(const Ram &)> &reader) {
    psprecomp::Runtime *rt = state().runtime;
    if (rt == nullptr) return;
    const GuestRam ram(rt->memory());
    reader(ram);
}

void frame(psprecomp::Runtime &runtime) {
    if (!enabled()) return;
    State &s = state();
    s.runtime = &runtime;
    GuestRam ram(runtime.memory());
    run_pending(ram);
    console_frame(ram);
    if (blocked_reason().empty()) held_cheats_frame(ram, s.held);
    s.quest = quest_lines(ram);
}

HeldCheats held_cheats() {
    return state().held;
}

void set_held_cheats(const HeldCheats &cheats) {
    HeldCheats &held = state().held;
    const auto note = [](const char *name, bool before, bool after) {
        if (before != after) log(std::string(name) + (after ? " on" : " off"));
    };
    note("infinite health", held.health, cheats.health);
    note("infinite stamina", held.stamina, cheats.stamina);
    note("frozen quest timer", held.timer, cheats.timer);
    note("monsters at 1 health", held.one_hit, cheats.one_hit);
    held = cheats;
}

std::vector<std::string> quest_status() {
    return state().quest;
}

const std::vector<quests::Quest> &board_quests() {
    static std::vector<quests::Quest> list;
    static bool read = false;
    if (!read) {
        read = true;
        list = quests::board_quests(&read_entry);
        std::size_t hall = 0u;
        for (const quests::Quest &q : list) hall += q.board() == quests::Board::Hall ? 1u : 0u;
        log("read " + std::to_string(list.size() - hall) + " village and " + std::to_string(hall) +
            " Guild Hall quests from the game's quest lists");
    }
    return list;
}

const quests::Quest *find_quest(std::uint16_t id) {
    for (const quests::Quest &q : board_quests())
        if (q.id == id) return &q;
    return nullptr;
}

void request_quest_start(std::uint16_t id) {
    const quests::Quest *quest = find_quest(id);
    if (quest == nullptr) {
        log("not started: " + std::to_string(id) + " is not a village or Hall quest the game's lists hold");
        return;
    }
    const quests::Quest copy = *quest;
    request("start quest " + std::to_string(id), [copy](Ram &ram) { return quests::start(ram, copy); });
}

void log(const std::string &line) {
    std::cout << "[debug] " << line << std::endl;
    std::deque<std::string> &recent = recent_lines();
    recent.push_back(line);
    while (recent.size() > kRecentLines) recent.pop_front();
}

std::vector<std::string> recent_log() {
    return {recent_lines().begin(), recent_lines().end()};
}

} // namespace mhp2g::debug
