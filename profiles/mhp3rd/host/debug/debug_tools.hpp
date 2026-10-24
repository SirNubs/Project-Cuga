#pragma once

// Developer tools: the Debug page of the in-game menu (cheats for testing) and
// the command file that drives the same tools from a script. They exist only
// in developer builds: MHP3RD_DEBUG_MENU is off in release builds, which then
// compile none of this, and a developer build shows them only when the
// environment variable MHP3RD_DEBUG_MENU=1 is set.
//
// Nothing here writes guest memory from the interface directly. The menu
// queues a request; the requests run at the game's flip, between two game
// frames, on the thread that runs the game (debug::frame from present_frame).
// While ad hoc play is active the tools refuse to write anything, so a test
// never reaches another player's game.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace psprecomp {
class Runtime;
}

namespace mhp3rd::game {
class Ram;
}

namespace mhp3rd::debug {

namespace quests {
struct Quest;
}

using game::Ram;

// Compiled in and switched on for this run (MHP3RD_DEBUG_MENU=1).
[[nodiscard]] bool enabled();

// Whether writes are refused right now, and why ("" when they are allowed):
// during ad hoc play.
[[nodiscard]] std::string blocked_reason();

// Queues a change to guest memory. It runs between two game frames: at once
// when the menu calls it (the menu runs at the flip), otherwise at the next
// flip. `change` returns the line to log, what it did; `what` names it in the
// log when it is refused.
void request(std::string what, std::function<std::string(Ram &)> change);

// Reads guest memory now. Only for the menu, which runs at the flip too (see
// hle_media.cpp), and never for writing.
void read(const std::function<void(const Ram &)> &reader);

// At the flip, on the game's thread: runs queued requests, keeps the held
// cheats (infinite health and so on) applied, and reads the command file.
void frame(psprecomp::Runtime &runtime);

// Cheats held while they are on: applied again at every flip.
struct HeldCheats {
    bool health{};
    bool stamina{};
    bool timer{};
    bool one_hit{};
    bool operator==(const HeldCheats &) const = default;
};
[[nodiscard]] HeldCheats held_cheats();
// Logs what was switched on or off.
void set_held_cheats(const HeldCheats &cheats);

// Lines about the quest in progress (the monsters' health, the clock), read
// at the last flip. Empty outside a quest.
[[nodiscard]] std::vector<std::string> quest_status();

// The village and Guild Hall quests from the game's own quest lists in
// DATA.BIN, read once, as the game shows them (a mod's lists included). Empty
// without a disc.
[[nodiscard]] const std::vector<quests::Quest> &board_quests();
[[nodiscard]] const quests::Quest *find_quest(std::uint16_t id);
// Queues a start of the quest (quest_start.hpp); logs what happened.
void request_quest_start(std::uint16_t id);

// Logs a "[debug] ..." line.
void log(const std::string &line);
// The last lines logged, oldest first, for the Debug page.
[[nodiscard]] std::vector<std::string> recent_log();

} // namespace mhp3rd::debug
