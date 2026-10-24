#pragma once

#include "imgui.h"

// The menu's Text section: the font the game's text is drawn with, its
// weight, a preview, and the list of fonts to choose from.
namespace mhp2g::ui {

// The section's rows, for the menu's Video page.
void font_rows();

// Whether the list of fonts is open.
[[nodiscard]] bool font_list_open();

// Draws the list of fonts while it is open. `back`: the back button was
// pressed this frame, which closes the list. True while the list is open, so
// the menu keeps itself open too.
bool font_list(bool back);

// Draws a line of text with the glyphs and the layout the game itself uses (fonts/game_font.hpp): each
// character's cell as a sprite `size` pixels tall, half as wide for Latin letters. For the port's own prompts
// among the game's. A line is kept ready for each `slot` (0..7), so several lines can be up at once.
void draw_game_text(ImDrawList *draw, ImVec2 at, float size, const char32_t *text, ImU32 colour, int slot = 0);

} // namespace mhp2g::ui
