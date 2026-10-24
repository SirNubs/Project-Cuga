#pragma once

#include <cstdint>
#include <string>

namespace psprecomp {
class Runtime;
struct AllegrexContext;
} // namespace psprecomp

// A lab tool towards new weapon movesets: the game starts a hunter's motion with its function at
// 0x088D85BC (a0 the hunter, a1 the motion id: 1000 + 100 * table + slot for the weapon's motion
// pack, below 1000 for the common pack). With MHP2G_MOTION_MAP=<file>, each line "from to" of that
// file replaces motion `from` by `to` as the game asks for it; the file is read again whenever it
// changes, so motions can be tried without restarting. Off, and nothing registered, without it.
namespace mhp2g::game {

using MotionFunction = void (*)(psprecomp::Runtime &, psprecomp::AllegrexContext &);
// `original`: the generated unit that holds 0x088D85BC.
void prepare_motion_map(psprecomp::Runtime &runtime, MotionFunction original);
// Once a flip: opens the cancel windows the file gives ("cancel <motion> <seconds>").
void motion_map_frame(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx);
// PSP buttons to press for the game now: an attack that was pressed early, at its cancel time.
[[nodiscard]] unsigned motion_map_buttons();
// The pad as the game is to see it, from the pad as it is: with a guard rule and the weapon drawn, R and what
// is pressed with it are the rules' own (guard, R + button moves) and are kept from the game.
[[nodiscard]] unsigned motion_map_pad(unsigned buttons);
// A note for the screen since the last call ("Long mode"), or nothing.
// The left stick as the game is about to read it (0x80 = middle): for the rules' dash (the stick pushed twice).
void motion_map_stick(std::uint8_t analog_x, std::uint8_t analog_y);
[[nodiscard]] std::string motion_map_take_note();

} // namespace mhp2g::game
