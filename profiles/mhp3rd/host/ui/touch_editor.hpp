#pragma once

// The editor of the action touch layout (#174), opened from the menu's
// Controls page: the layout drawn full screen over the game, each element
// dragged where it should be, and a small panel to resize, rebind and hide
// the element chosen, set the opacity and the haptic feedback, and reset.
// Every change is saved at once.
namespace mhp3rd::ui {

void open_touch_editor();
[[nodiscard]] bool touch_editor_open();
// One frame of the editor, drawn instead of the menu's panel. `back` (Esc,
// the pad's back button) closes it.
void touch_editor_frame(bool back);

} // namespace mhp3rd::ui
