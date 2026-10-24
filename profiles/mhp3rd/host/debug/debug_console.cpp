#include "debug/debug_console.hpp"

#include "debug/debug_tools.hpp"
#include "debug/game_state.hpp"
#include "debug/quest_start.hpp"
#include "game/guest_ram.hpp"
#include "platform/utf8_path.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace mhp3rd::debug {
namespace {

// User memory: the game and its heaps. The kernel's first 8 MiB hold nothing
// of the game's.
constexpr std::uint32_t kScanStart = 0x08800000u;
constexpr std::uint32_t kScanEnd = 0x0C000000u;
constexpr std::size_t kMostPrinted = 32u;

std::string hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08X", value);
    return text;
}

std::uint32_t number(const std::string &text) {
    return static_cast<std::uint32_t>(std::strtoll(text.c_str(), nullptr, 0));
}

// A memory search, narrowed a step at a time like a cheat searcher's: a first
// scan for a value, then filters on what the survivors hold later.
struct Search {
    unsigned width{4u};
    std::vector<std::uint32_t> places;
    std::vector<std::uint32_t> values; // what each held at the last step
};

Search &search() {
    static Search s;
    return s;
}

std::uint32_t load(const Ram &ram, std::uint32_t address, unsigned width) {
    switch (width) {
    case 1u:
        return ram.load8(address);
    case 2u:
        return ram.load16(address);
    default:
        return ram.load32(address);
    }
}

std::uint32_t scan_end(const Ram &ram) {
    std::uint32_t end = kScanEnd;
    while (end > kScanStart && !ram.contains(end - 4u, 4u)) end -= 0x100000u;
    return end;
}

void summary(std::vector<std::string> &out) {
    const Search &s = search();
    std::string line = std::to_string(s.places.size()) + " places:";
    for (std::size_t i = 0; i < s.places.size() && i < kMostPrinted; ++i)
        line += " " + hex(s.places[i]) + "=" + std::to_string(s.values[i]);
    out.push_back(line);
}

template <typename Keep> void narrow(const Ram &ram, Keep keep) {
    Search &s = search();
    std::vector<std::uint32_t> places;
    std::vector<std::uint32_t> values;
    for (std::size_t i = 0; i < s.places.size(); ++i) {
        const std::uint32_t now = load(ram, s.places[i], s.width);
        if (keep(s.values[i], now)) {
            places.push_back(s.places[i]);
            values.push_back(now);
        }
    }
    s.places = std::move(places);
    s.values = std::move(values);
}

std::vector<std::uint8_t> parse_bytes(const std::string &text) {
    std::vector<std::uint8_t> bytes;
    for (std::size_t i = 0; i + 1u < text.size(); i += 2u)
        bytes.push_back(static_cast<std::uint8_t>(std::strtoul(text.substr(i, 2u).c_str(), nullptr, 16)));
    return bytes;
}

} // namespace

std::vector<std::string> run_command(Ram &ram, const std::string &line) {
    std::vector<std::string> out;
    std::istringstream words(line);
    std::string command;
    words >> command;
    std::vector<std::string> args;
    for (std::string word; words >> word;) args.push_back(word);
    const auto arg = [&](std::size_t i) { return i < args.size() ? number(args[i]) : 0u; };

    if (command.empty() || command[0] == '#') return out;
    if (command == "find8" || command == "find16" || command == "find32") {
        Search &s = search();
        s.width = command == "find8" ? 1u : command == "find16" ? 2u : 4u;
        const std::uint32_t wanted = arg(0);
        const std::uint32_t start = args.size() > 1u ? arg(1) : kScanStart;
        const std::uint32_t end = args.size() > 2u ? arg(2) : scan_end(ram);
        s.places.clear();
        s.values.clear();
        for (std::uint32_t a = start; a + s.width <= end; a += s.width)
            if (load(ram, a, s.width) == wanted) {
                s.places.push_back(a);
                s.values.push_back(wanted);
            }
        summary(out);
    } else if (command == "next") {
        const std::uint32_t wanted = arg(0);
        narrow(ram, [&](std::uint32_t, std::uint32_t now) { return now == wanted; });
        summary(out);
    } else if (command == "changed" || command == "unchanged") {
        const bool want_change = command == "changed";
        narrow(ram, [&](std::uint32_t before, std::uint32_t now) { return (before != now) == want_change; });
        summary(out);
    } else if (command == "delta") {
        const auto d = static_cast<std::int32_t>(arg(0));
        narrow(
            ram, [&](std::uint32_t before, std::uint32_t now) { return static_cast<std::int32_t>(now - before) == d; });
        summary(out);
    } else if (command == "list") {
        summary(out);
    } else if (command == "findbytes") {
        const std::vector<std::uint8_t> bytes = parse_bytes(args.empty() ? std::string() : args[0]);
        std::size_t found = 0u;
        std::string text = "bytes at:";
        const std::uint32_t end = scan_end(ram);
        if (!bytes.empty())
            for (std::uint32_t a = kScanStart; a + bytes.size() <= end; ++a) {
                bool match = true;
                for (std::size_t i = 0; i < bytes.size() && match; ++i)
                    match = ram.load8(a + static_cast<std::uint32_t>(i)) == bytes[i];
                if (match && found++ < 64u) text += " " + hex(a);
            }
        out.push_back(text + " (" + std::to_string(found) + ")");
    } else if (command == "peek") {
        const std::uint32_t address = arg(0);
        const std::uint32_t length = args.size() > 1u ? arg(1) : 64u;
        for (std::uint32_t row = 0; row < length; row += 16u) {
            std::string text = hex(address + row) + ":";
            for (std::uint32_t i = 0; i < 16u && row + i < length; ++i) {
                if (!ram.contains(address + row + i, 1u)) break;
                char b[4];
                std::snprintf(b, sizeof(b), " %02X", ram.load8(address + row + i));
                text += b;
            }
            out.push_back(text);
        }
    } else if (command == "poke8" || command == "poke16" || command == "poke32") {
        const std::uint32_t address = arg(0);
        const std::uint32_t value = arg(1);
        const unsigned width = command == "poke8" ? 1u : command == "poke16" ? 2u : 4u;
        if (!blocked_reason().empty()) {
            out.push_back("refused: " + blocked_reason());
        } else if (!ram.contains(address, width)) {
            out.push_back("not guest memory: " + hex(address));
        } else {
            if (width == 1u)
                ram.store8(address, static_cast<std::uint8_t>(value));
            else if (width == 2u)
                ram.store16(address, static_cast<std::uint16_t>(value));
            else
                ram.store32(address, value);
            out.push_back(command + " " + hex(address) + " <- " + std::to_string(value));
        }
    } else if (command == "dump") {
        const std::string path = args.empty() ? std::string("ram.bin") : args[0];
        const std::uint32_t start = args.size() > 1u ? arg(1) : 0x08000000u;
        const std::uint32_t end = args.size() > 2u ? start + arg(2) : scan_end(ram);
        std::ofstream file(path, std::ios::binary);
        std::vector<char> bytes;
        bytes.reserve(end - start);
        for (std::uint32_t a = start; a < end && ram.contains(a, 1u); ++a)
            bytes.push_back(static_cast<char>(ram.load8(a)));
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.push_back("dumped " + std::to_string(bytes.size()) + " bytes from " + hex(start) + " to " + path);
    } else if (command == "quest" && !args.empty() && args[0] == "list") {
        // quest list [village|hall] [STARS]: the quests, or some of them.
        std::size_t next = 1u;
        std::optional<quests::Board> board;
        if (args.size() > 1u && (args[1] == "village" || args[1] == "hall")) {
            board = args[1] == "hall" ? quests::Board::Hall : quests::Board::Village;
            ++next;
        }
        const std::uint32_t stars = arg(next);
        std::size_t shown = 0u;
        for (const quests::Quest &q : board_quests()) {
            if ((board && q.board() != *board) || (stars != 0u && q.stars != stars)) continue;
            out.push_back("  " + std::to_string(q.id) + "  " +
                (q.board() == quests::Board::Hall ? "Hall " : "village ") + std::to_string(q.stars) + " star  " +
                q.name + "  (" + quests::monster_list(q) + "; fee " + std::to_string(q.fee) + "z)");
            ++shown;
        }
        out.push_back(std::to_string(shown) + " quests" +
            (board_quests().empty() ? std::string(" (the game's quest lists could not be read)") : std::string()));
    } else if (command == "quest" && !args.empty() && args[0] == "start") {
        // quest start ID: leave for that quest, as its gate does.
        if (args.size() < 2u) {
            out.push_back("usage: quest start ID (see quest list)");
        } else if (!blocked_reason().empty()) {
            out.push_back("refused (" + blocked_reason() + "): " + line);
        } else if (const quests::Quest *q = find_quest(static_cast<std::uint16_t>(arg(1))); q == nullptr) {
            out.push_back("not started: " + args[1] + " is not a village or Hall quest the game's lists hold");
        } else {
            out.push_back(quests::start(ram, *q));
        }
    } else if (command != "state" && command != "item" && command != "table" && command != "quest" &&
        !blocked_reason().empty()) {
        out.push_back("refused (" + blocked_reason() + "): " + line);
    } else if (!game_command(ram, command, args, out)) {
        out.push_back("unknown command: " + command);
    }
    return out;
}

void console_frame(Ram &ram) {
    static const std::filesystem::path path = environment_path("MHP3RD_DEBUG_COMMANDS");
    if (path.empty()) return;
    static std::streamoff offset = [] {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        log("reading commands from " + path_to_utf8(path));
        return file ? static_cast<std::streamoff>(file.tellg()) : std::streamoff{0};
    }();
    std::ifstream file(path, std::ios::binary);
    if (!file) return;
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < offset) offset = 0;
    if (size == offset) return;
    file.seekg(offset);
    std::string line;
    while (std::getline(file, line)) {
        if (file.eof()) break; // an incomplete last line: read it next time
        offset = file.tellg();
        log("> " + line);
        for (const std::string &answer : run_command(ram, line)) log(answer);
    }
}

} // namespace mhp3rd::debug
