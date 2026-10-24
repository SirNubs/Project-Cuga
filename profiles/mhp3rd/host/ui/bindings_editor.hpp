#pragma once

#include "input/bindings.hpp"

#include <cstddef>
#include <string>

// The bindings of the controls page (#188): every action in its group, its
// keyboard and mouse bindings beside its gamepad ones. Each binding is a
// chip: confirm rebinds it, the top face button, Delete, a right click or its
// × clears it. A "+" chip adds one more, and a reset chip puts the action
// back as the preset it was made from has it. Conflicts are listed under
// their action with a one-click fix. Works with a gamepad alone, a keyboard,
// a mouse and touch.
namespace mhp3rd::ui {

// Draws the bindings into the menu's content. `notice` receives what an edit
// did to the presets (a new preset of the player's, made from a shipped one).
void bindings_editor(std::string &notice);

// What has the focus in the bindings, for the footer's button hints.
enum class BindingsFocus { None, Binding, Add, Reset, Fix };
[[nodiscard]] BindingsFocus bindings_focus();
// Whether the focused action differs from its preset's default.
[[nodiscard]] bool bindings_focus_resettable();

// An action's bindings in one line, the keyboard's then the gamepad's as
// the pad in use labels them: "F12 / PrintScreen; RS + D-pad Left". Empty
// when neither device has any.
[[nodiscard]] std::string bindings_summary(input::Action action);

// How many actions have a conflict the player has not chosen to keep.
[[nodiscard]] std::size_t bindings_conflicts();

// While a binding is captured: the prompt over the whole menu, saying what
// is being bound and how to cancel. Call after the panel is drawn.
void bindings_capture_prompt();

} // namespace mhp3rd::ui
