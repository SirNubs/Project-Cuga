#pragma once

union SDL_Event;

// Controls > Controllers (#147): every controller connected, what SDL makes
// of it (name, USB ids, GUID, mapping, buttons, axes and hats), what each of
// its inputs reports right now, and a setup that turns a controller SDL does
// not know into a gamepad the game and the menu read.
namespace mhp3rd::ui {

// The Controls page's first section: a row that opens the screen, marked as
// a warning while a controller is connected that nothing reads yet.
void controllers_rows();

[[nodiscard]] bool controllers_screen_open();

// Draws the screen while it is open. `back`: the back button was pressed this
// frame, which cancels the setup or closes the screen. True while it is open,
// so the menu keeps itself open too.
bool controllers_screen(bool back);

// A note over the game when a controller without a mapping connects, or one
// of its buttons is pressed (now and then): it does nothing until it is set
// up, and silence would look like a broken port.
void note_unknown_controller(const SDL_Event &event);
// The same for the controllers connected at start.
void note_unknown_controllers();

} // namespace mhp3rd::ui
