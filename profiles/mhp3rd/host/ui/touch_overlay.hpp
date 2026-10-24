#pragma once

#include "input/touch_action.hpp"
#include "input/touch_controls.hpp"

#include <cstdint>
#include <optional>

struct ImDrawList;

namespace mhp3rd::ui {

// Draws the on-screen touch controls over the game in the current ImGui frame,
// behind any window: outlines at `opacity`, filled while held.
void draw_touch_controls(const input::touch::Controls &controls, float opacity);

// The action layout (#174) the same way, `ms` for the swipes' marks.
void draw_action_controls(const input::touch::ActionControls &controls, float opacity, std::uint64_t ms);

// One element of the action layout, for the game and for the editor: its
// own icon, drawn in code, and the PSP buttons it presses beside it.
struct ElementLook {
    float opacity{0.5f};
    bool held{};
    bool selected{}; // the editor's choice: an accent ring
    bool hidden{};   // the editor shows hidden elements, struck through
    int swipe{};     // the swipe area: -1 or 1 while a swipe presses
};
void draw_action_element(ImDrawList *list, input::touch::Element element, const input::touch::Placement &placement,
    const input::touch::Placed &placed, const input::touch::Point &stick_thumb, const ElementLook &look);

} // namespace mhp3rd::ui
