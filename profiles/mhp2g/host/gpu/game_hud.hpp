#pragma once

#include <cstdint>
#include <string>

namespace psprecomp {
class Runtime;
}

// Hiding the game's HUD (issue #184): the health and stamina bars, the clock,
// sharpness, the item bar, the map, name tags and the other labels that
// follow characters, and the prompts in the corners. Menus, dialogs, fades,
// cutscenes and loading screens stay.
//
// How the game's 2D draws were told apart, traced with MHP2G_TRACE_HUD and
// MHP2G_TRACE_SPRITES in the village, the Guild Hall and a quest
// (NPJB-40001, the one executable Yakumo supports):
//
// - Every 2D element is a packet of GE commands the game files into its
//   display list's ordering table with 0x08876874 (a0 the list object, a1
//   the packet, a2 its length in words, a3 the bucket). Buckets are drawn in
//   order. The HUD shares its buckets with menus and dialogs (12 holds the
//   quest HUD and the quest counter's lists, 13 the hall's status list and
//   every dialog box), so the bucket alone cannot tell them apart.
// - The code that asks for the packet can: every HUD element is drawn from
//   inside one of two functions, 0x08945D78 in the village and the Guild
//   Hall (name tags, the "Select Target" and "Hide List" prompts, the status
//   list) and 0x0A17370C in a quest (game_task.ovl's HUD: bars, clock,
//   sharpness, name plate, item bar, map). Menus and dialogs are drawn by
//   other code. So a packet is HUD when one of the two is on the guest's call
//   stack when it is filed; the stack is read the way the game's code builds
//   it (each function's addiu sp, sp, -N and sw ra).
// - Most text is not drawn where it is asked for: the game queues it
//   (0x09FF0C28 points at the text context, which keeps seven queues) and
//   draws every queue at the end of the frame, far from the code that asked.
//   Each call into the text functions from outside them is watched instead,
//   and the queue entries added until the next watched call or packet belong
//   to the code on the stack then. When the queue is drawn, the entry each
//   glyph comes from is read off the drawing function's stack frame, and so is
//   the icon list entry of each button icon in the text.
// - The GE draws a list while the game fills the next elsewhere, and the
//   photo mode draws the last one again, so a HUD packet's range is kept until
//   another packet is filed over it.
//
// The renderer then leaves out the draws of HUD packets, and only those,
// while the HUD is hidden. The packets' other commands still run, so the GE
// state they set is the same either way.
namespace mhp2g::gpu::hud {

// Called at every flip, where guest functions may be wrapped: puts the
// wrappers in place once the HUD is first hidden (or MHP2G_TRACE_HUD asks).
void frame(psprecomp::Runtime &runtime, std::uint64_t presented_frames);

// The player's switch (the Hide HUD bind and the menu).
void set_hidden(bool hidden);
void toggle();
[[nodiscard]] bool hidden();
// While the free camera flies (or holds a photo), with
// experimental.free_camera_hide_hud on, the HUD is hidden too.
void set_free_camera(bool active);
// Whether the renderer leaves the HUD out now.
[[nodiscard]] bool active();
// False when the game's code is not the code the classification was traced
// on: the switch then does nothing.
[[nodiscard]] bool available();

// A short note after the switch changes: its text, and how long it has left.
[[nodiscard]] double note_seconds_left();
[[nodiscard]] std::string note_text();

// True when a through-mode draw belongs to the HUD and the HUD is hidden.
// `command_address` is its PRIM command, `call_return` where the list's
// innermost CALL returns to.
[[nodiscard]] bool hides(std::uint32_t command_address, std::uint32_t call_return);

} // namespace mhp2g::gpu::hud
