#pragma once

// Starting a quest from the developer tools: the village and Guild Hall
// quests the game has, read from its own quest lists, and a departure made the
// way the game makes one when the hunter presses the button at the village
// gate or the Hall's.
//
// The game keeps the accepted quest's id in the quest state beside the
// character (0x09FAF8C2). Departing is a scene change the village or the Hall
// asks for, through the same function for both: it writes the gate the hunter
// leaves by (which is also where the hunter comes back to), sets the next
// scene to "quest" and asks the scene to end. The quest overlay then loads the quest by its
// id, as it does after the counter and the gate, so the map, the monsters, the
// clock and the rewards are the game's own. docs/DEBUG_MENU.md says how each
// address was traced.
//
// Pure functions over a Ram and over file bytes, so the unit tests run them
// on buffers. Nothing here writes on its own: debug_tools.cpp calls these at
// the flip.

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mhp3rd::game {
class Ram;
}

namespace mhp3rd::debug::quests {

using game::Ram;

// Where a quest is taken: the Yukumo Chief in the village, or the Gathering
// Hall's counter.
enum class Board : std::uint8_t { Village, Hall };

// One quest from the game's quest lists.
struct Quest {
    std::uint16_t id{};
    std::uint8_t stars{};
    std::uint32_t fee{};        // zenny the counter takes when it is accepted
    std::uint32_t reward{};     // zenny for completing it
    std::uint32_t time_limit{}; // frames at 30 a second
    std::string name;
    std::string objective;
    std::string monsters; // the "Main Monster" lines, one per line
    std::string client;

    [[nodiscard]] Board board() const;
};

// The game's quest lists in DATA.BIN, one per star level: entries 4059 to
// 4066 for levels 1 to 8. Levels 1 to 6 hold the village quests of that level
// (ids 101 to 699, the level in the hundreds) together with the Gathering
// Hall's (ids 1SSNN: 10101 to 10899, the level in SS); levels 7 and 8 hold
// only Hall quests.
inline constexpr std::uint32_t kFirstQuestList = 4059u;
inline constexpr std::uint32_t kQuestLists = 8u;

// A quest list: 32-bit offsets to the records, ended by 0; each record has the
// fee at +4, the reward at +8, the time limit at +0x10, the id (u16) at +0x1C,
// the stars at +0x1E, and from +0x48 the name, the objective, the failure
// conditions, the description, the main monsters and the client, each ended by
// a zero byte. Records that do not look like quests are left out; an empty
// result means the bytes are not a quest list.
[[nodiscard]] std::vector<Quest> parse_quest_list(std::span<const std::uint8_t> file);

// The main monsters on one line: "Jaggi, Gargwa".
[[nodiscard]] std::string monster_list(const Quest &quest);

// Village quests have ids 101 to 699, Hall quests 10101 to 10899: the stars
// in the hundreds.
[[nodiscard]] bool village_quest(std::uint16_t id);
[[nodiscard]] bool hall_quest(std::uint16_t id);

// The village and Hall quests of every level: the village's by star level,
// then the Hall's by star level, each as the lists order them. Event and
// training quests are not in these lists. `read_entry` returns a DATA.BIN
// entry's plain bytes, or nothing.
[[nodiscard]] std::vector<Quest> board_quests(
    const std::function<std::optional<std::vector<std::uint8_t>>(std::uint32_t entry)> &read_entry);

// The game's memory ------------------------------------------------------------

// The quest state beside the character: the id of the accepted quest (u16;
// the village writes 1 when it loads, for none), and the gate the hunter
// leaves by (u8: 2 the village gate, 3 the Guild Hall's; -1 after loading
// a save) with a flag after it. When the village loads again after a quest it
// puts the hunter at that gate: back in the village, or in the Hall. The game
// reaches them through the character pointer (0x08AB3640) plus 0x60472 and
// 0x60478.
inline constexpr std::uint32_t kCharacterPointer = 0x08AB3640u;
inline constexpr std::uint32_t kQuestIdOffset = 0x60472u;
inline constexpr std::uint32_t kGateOffset = 0x60478u;
inline constexpr std::uint32_t kGateFlagOffset = 0x60479u;
inline constexpr std::uint8_t kVillageGate = 2u;
inline constexpr std::uint8_t kHallGate = 3u;

// The map overlay in its slot names where the hunter is: P_v00a00 the village,
// P_v00a01 the Guild Hall (a02 to a04 the other village areas).
inline constexpr std::uint32_t kMapSlot = 0x0A055E80u;

// The next scene: a pointer at 0x0A25DD28 to a word the village and the Hall
// set while they run (0 while the hunter walks around, other values while a
// menu or a dialog is open and when leaving), and
// 30 when the scene that ends is to become a quest. A word at +0x28 is set to
// -1 with it.
inline constexpr std::uint32_t kNextScenePointer = 0x0A25DD28u;
inline constexpr std::uint32_t kSceneQuest = 30u;
inline constexpr std::uint32_t kSceneWalking = 0u;

// The village scene object (pointer at 0x08ABAE14): bit 4 of its flags at
// +0x20 asks it to end, which it does over the next frames, switching to the
// scene set above.
inline constexpr std::uint32_t kScenePointer = 0x08ABAE14u;
inline constexpr std::uint32_t kSceneFlags = 0x20u;
inline constexpr std::uint32_t kSceneEnd = 4u;

// The flag the game copies beside the gate: bit 0 of a word it reaches through
// the pointer at 0x09FC8BE8, plus 0x01500000 - 0x57DC.
inline constexpr std::uint32_t kStatePointer = 0x09FC8BE8u;
inline constexpr std::uint32_t kGateFlagSource = 0x01500000u - 0x57DCu;

// Why a quest cannot be started now, or "" when it can: the hunter must be in
// the village or the Guild Hall (not on a quest, not in the house, on the
// farm or at the hot spring), walking around with no menu or dialog open, and
// not already leaving.
[[nodiscard]] std::string start_blocked(const Ram &ram);

// Starts `quest` the way its own gate does: takes the counter's fee, sets the
// quest's id, and leaves by the village gate for a village quest or by the
// Hall's for a Hall quest, from either place: the hunter comes back where the
// quest belongs. Returns the line to log. Refused (and nothing written) when
// start_blocked says so, when the quest is neither a village nor a Hall quest,
// or when the hunter cannot pay the fee.
std::string start(Ram &ram, const Quest &quest);

} // namespace mhp3rd::debug::quests
