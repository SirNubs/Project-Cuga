#include "debug/quest_start.hpp"

#include "debug/game_state.hpp"
#include "game/game_data.hpp"
#include "game/guest_ram.hpp"

#include <algorithm>
#include <cstdio>
#include <iterator>

namespace mhp2g::debug::quests {
namespace {

constexpr std::size_t kRecordHeader = 0x48u;
constexpr std::size_t kRecordStrings = 6u; // name, objective, failure, description, monsters, client

std::uint32_t load32(std::span<const std::uint8_t> bytes, std::size_t at) {
    if (at + 4u > bytes.size()) return 0u;
    return static_cast<std::uint32_t>(bytes[at]) | static_cast<std::uint32_t>(bytes[at + 1u]) << 8u |
        static_cast<std::uint32_t>(bytes[at + 2u]) << 16u | static_cast<std::uint32_t>(bytes[at + 3u]) << 24u;
}

// Text as the lists hold it: printable bytes, line breaks and UTF-8.
bool plain_text(const std::string &text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
        const auto b = static_cast<unsigned char>(c);
        return b >= 0x20u || b == '\n';
    });
}

// The zero-ended strings after a record's header, skipping the padding
// between them, up to the next record.
std::vector<std::string> record_strings(std::span<const std::uint8_t> file, std::size_t from, std::size_t end) {
    std::vector<std::string> out;
    std::size_t at = from;
    while (at < end && out.size() < kRecordStrings) {
        if (file[at] == 0u) {
            ++at;
            continue;
        }
        std::string text;
        while (at < end && file[at] != 0u) text += static_cast<char>(file[at++]);
        if (!plain_text(text)) break;
        out.push_back(std::move(text));
    }
    return out;
}

std::string money_text(std::uint32_t zenny) {
    return std::to_string(zenny) + "z";
}

std::uint32_t character(const Ram &ram) {
    return ram.load32(kCharacterPointer);
}

std::optional<std::uint32_t> pointer(const Ram &ram, std::uint32_t at, std::uint32_t length) {
    if (!ram.contains(at, 4u)) return std::nullopt;
    const std::uint32_t value = ram.load32(at);
    if (value < 0x08800000u || !ram.contains(value, length)) return std::nullopt;
    return value;
}

} // namespace

std::vector<Quest> parse_quest_list(std::span<const std::uint8_t> file) {
    std::vector<Quest> quests;
    // The offsets, ended by 0; the first record follows them.
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; 4u * i + 4u <= file.size(); ++i) {
        const std::uint32_t offset = load32(file, 4u * i);
        if (offset == 0u) break;
        offsets.push_back(offset);
    }
    if (offsets.empty() || offsets.front() != 4u * (offsets.size() + 1u)) return quests;
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        const std::size_t at = offsets[i];
        const std::size_t end = i + 1u < offsets.size() ? offsets[i + 1u] : file.size();
        if (end <= at || at + kRecordHeader > file.size() || end > file.size()) continue;
        Quest q;
        q.fee = load32(file, at + 0x04u);
        q.reward = load32(file, at + 0x08u);
        q.time_limit = load32(file, at + 0x10u);
        q.id = static_cast<std::uint16_t>(load32(file, at + 0x1Cu) & 0xFFFFu);
        q.stars = file[at + 0x1Eu];
        const std::vector<std::string> text = record_strings(file, at + kRecordHeader, end);
        if (q.id == 0u || text.empty()) continue;
        q.name = text[0];
        if (text.size() > 1u) q.objective = text[1];
        if (text.size() > 4u) q.monsters = text[4];
        if (text.size() > 5u) q.client = text[5];
        quests.push_back(std::move(q));
    }
    return quests;
}

std::string monster_list(const Quest &quest) {
    std::string out;
    std::string line;
    const auto flush = [&] {
        const std::size_t first = line.find_first_not_of(' ');
        const std::size_t last = line.find_last_not_of(' ');
        if (first != std::string::npos) out += (out.empty() ? "" : ", ") + line.substr(first, last - first + 1u);
        line.clear();
    };
    for (const char c : quest.monsters) {
        if (c == '\n')
            flush();
        else
            line += c;
    }
    flush();
    return out;
}

bool village_quest(std::uint16_t id) {
    return id >= 101u && id <= 699u && id % 100u != 0u;
}

bool hall_quest(std::uint16_t id) {
    return id >= 10101u && id <= 10899u && (id / 100u) % 100u != 0u && id % 100u != 0u;
}

Board Quest::board() const {
    return hall_quest(id) ? Board::Hall : Board::Village;
}

std::vector<Quest> board_quests(
    const std::function<std::optional<std::vector<std::uint8_t>>(std::uint32_t entry)> &read_entry) {
    std::vector<Quest> village;
    std::vector<Quest> hall;
    for (std::uint32_t level = 1; level <= kQuestLists; ++level) {
        const std::optional<std::vector<std::uint8_t>> file = read_entry(kFirstQuestList + level - 1u);
        if (!file) continue;
        for (Quest &q : parse_quest_list(*file)) {
            if (village_quest(q.id) && q.id / 100u == level)
                village.push_back(std::move(q));
            else if (hall_quest(q.id) && (q.id / 100u) % 100u == level)
                hall.push_back(std::move(q));
        }
    }
    village.insert(village.end(), std::make_move_iterator(hall.begin()), std::make_move_iterator(hall.end()));
    return village;
}

std::string start_blocked(const Ram &ram) {
    if (!game::character_loaded(ram)) return "no character loaded";
    if (p3rd::on_quest(ram)) return "a quest is already running";
    if (game::read_text(ram, p3rd::kTaskSlot + 32u) != "lobby_task.ovl") return "not in the village";
    const std::string map = game::read_text(ram, kMapSlot + 32u);
    if (map.rfind("P_v00a00", 0) != 0u && map.rfind("P_v00a01", 0) != 0u)
        return "only in the village or the Guild Hall (not in the house, on the farm or at the hot spring)";
    const std::optional<std::uint32_t> next = pointer(ram, kNextScenePointer, 0x2Cu);
    const std::optional<std::uint32_t> scene = pointer(ram, kScenePointer, kSceneFlags + 4u);
    const std::uint32_t c = character(ram);
    if (!next || !scene || c < 0x08800000u || !ram.contains(c + kQuestIdOffset, 8u)) return "the village is not ready";
    if (ram.load32(*next) != kSceneWalking) return "only while the hunter walks around, with no menu or dialog open";
    if ((ram.load32(*scene + kSceneFlags) & kSceneEnd) != 0u) return "the village is already being left";
    return {};
}

std::string start(Ram &ram, const Quest &quest) {
    const std::string label = "quest " + std::to_string(quest.id) + " (" + quest.name + ")";
    if (const std::string why = start_blocked(ram); !why.empty()) return "not started, " + why + ": " + label;
    if (!village_quest(quest.id) && !hall_quest(quest.id))
        return "not started, only village and Guild Hall quests can be started: " + label;
    const bool hall = quest.board() == Board::Hall;
    const std::uint32_t zenny = p3rd::money(ram);
    if (zenny < quest.fee)
        return "not started, the fee is " + money_text(quest.fee) + " and the hunter has " + money_text(zenny) + ": " +
            label;

    // What the counter does when the quest is accepted: the fee, the id.
    p3rd::set_money(ram, zenny - quest.fee);
    const std::uint32_t c = character(ram);
    ram.store16(c + kQuestIdOffset, quest.id);

    // What the village or the Hall does when the hunter leaves by its gate, in
    // the game's order: the gate, the next scene, the flag beside the gate,
    // then the request that ends the scene.
    const std::uint32_t next = ram.load32(kNextScenePointer);
    const std::uint32_t scene = ram.load32(kScenePointer);
    ram.store32(next + 0x28u, 0xFFFFFFFFu);
    ram.store8(c + kGateOffset, hall ? kHallGate : kVillageGate);
    std::uint8_t flag = 0u;
    if (const std::uint32_t state = ram.load32(kStatePointer); state != 0u && ram.contains(state + kGateFlagSource, 4u))
        flag = static_cast<std::uint8_t>(ram.load32(state + kGateFlagSource) & 1u);
    ram.store32(next, kSceneQuest);
    ram.store8(c + kGateFlagOffset, flag);
    ram.store32(scene + kSceneFlags, ram.load32(scene + kSceneFlags) | kSceneEnd);

    char minutes[16];
    std::snprintf(minutes, sizeof(minutes), "%u min", quest.time_limit / 1800u);
    return "started " + label + ", " + (hall ? "Guild Hall " : "village ") + std::to_string(quest.stars) + " star, " +
        minutes + ", fee " + money_text(quest.fee) + " paid (zenny now " + std::to_string(p3rd::money(ram)) + ")";
}

} // namespace mhp2g::debug::quests
