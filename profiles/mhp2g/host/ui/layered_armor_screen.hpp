#pragma once

// The Layered armor page (game/layered_armor.hpp), opened from the Mods
// page: the switch, the piece chosen for each armor part, and the list of
// pieces to choose from, with the game's own names.
namespace mhp2g::ui {

// The row on the Mods page that opens the page.
void layered_armor_row();

// Draws the page, or the list of pieces it opened, while it is open; false
// when it is closed and the Mods page is to be drawn instead. `back`: the
// back button was pressed this frame.
bool layered_armor_screen(bool back);

// Whether the page is open, so back closes it before the menu.
[[nodiscard]] bool layered_armor_screen_open();

} // namespace mhp2g::ui
