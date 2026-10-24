#include "ui/touch_editor.hpp"

#include "ui/layer.hpp"
#include "ui/touch_overlay.hpp"
#include "ui/widgets.hpp"

#include "gpu/vulkan_renderer.hpp"
#include "input/touch_action.hpp"
#include "settings/settings.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace mhp3rd::ui {
namespace {

using input::touch::Element;

struct State {
    bool open{};
    std::optional<Element> selected;
    bool dragging{};
    ImVec2 grab{};       // the pointer's offset from the element's centre
    bool panel_bottom{}; // the player moved the panel down
};

State &state() {
    static State value;
    return value;
}

// The element under `at`, the smallest when they overlap, hidden ones
// included: the editor is where they are shown again.
std::optional<Element> element_at(
    const input::touch::ActionLayout &layout, const input::touch::Area &area, float scale, ImVec2 at) {
    std::optional<Element> best;
    float best_size = 0.0f;
    for (std::size_t i = 0; i < input::touch::kElements; ++i) {
        const auto e = static_cast<Element>(i);
        const input::touch::Placed p = input::touch::place(layout.elements[i], e, area, scale);
        const bool inside = e == Element::Swipe
            ? std::fabs(at.x - p.centre.x) <= p.half_width && std::fabs(at.y - p.centre.y) <= p.radius
            : std::hypot(at.x - p.centre.x, at.y - p.centre.y) <= std::max(p.radius * 1.2f, 12.0f);
        const float size = p.radius * p.half_width;
        if (inside && (!best || size < best_size)) {
            best = e;
            best_size = size;
        }
    }
    return best;
}

int cycle(int value, int delta, int count) {
    return ((value + delta) % count + count) % count;
}

} // namespace

void open_touch_editor() {
    State &s = state();
    s = State{};
    s.open = true;
    focus_next_row();
}

bool touch_editor_open() {
    return state().open;
}

void touch_editor_frame(bool back) {
    State &st = state();
    settings::Settings &s = settings::current();
    const ImGuiIO &io = ImGui::GetIO();
    const float font = Layer::get().font_size();
    const input::touch::ActionControls &live = Layer::get().renderer().action_touch_controls();
    const input::touch::Area area = live.area();
    const float scale = live.scale();
    input::touch::ActionLayout &layout = s.touch_action;

    // The game, dimmed a little, and the layout over it.
    ImDrawList *list = ImGui::GetBackgroundDrawList();
    list->AddRectFilled({0.0f, 0.0f}, io.DisplaySize, IM_COL32(0, 0, 0, 90));
    for (std::size_t i = 0; i < input::touch::kElements; ++i) {
        const auto e = static_cast<Element>(i);
        ElementLook look;
        look.opacity = std::max(s.touch_opacity, 0.6f);
        look.hidden = !layout.elements[i].shown;
        look.selected = st.selected == e;
        look.held = st.dragging && st.selected == e;
        draw_action_element(
            list, e, layout.elements[i], input::touch::place(layout.elements[i], e, area, scale), {}, look);
    }

    // Choosing and dragging, where the panel is not.
    if (!io.WantCaptureMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        st.selected = element_at(layout, area, scale, io.MousePos);
        st.dragging = st.selected.has_value();
        if (st.selected) {
            const input::touch::Placed p = input::touch::place(layout.at(*st.selected), *st.selected, area, scale);
            st.grab = {io.MousePos.x - p.centre.x, io.MousePos.y - p.centre.y};
        }
    }
    if (st.dragging && st.selected) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const input::touch::Point centre{io.MousePos.x - st.grab.x, io.MousePos.y - st.grab.y};
            layout.at(*st.selected) = input::touch::move_to(layout.at(*st.selected), *st.selected, centre, area);
        } else {
            st.dragging = false;
            settings::save();
        }
    }

    // The panel: at the top or the bottom with nothing chosen, beside the
    // element chosen on the other half of the screen otherwise.
    const float margin = font * 0.5f;
    float width = std::min(io.DisplaySize.x * 0.62f, font * 26.0f);
    ImVec2 at{io.DisplaySize.x * 0.5f, st.panel_bottom ? io.DisplaySize.y - margin : margin};
    ImVec2 pivot{0.5f, st.panel_bottom ? 1.0f : 0.0f};
    if (st.selected) {
        const input::touch::Placed p = input::touch::place(layout.at(*st.selected), *st.selected, area, scale);
        const bool element_right = p.centre.x > io.DisplaySize.x * 0.5f;
        width = std::min(io.DisplaySize.x * 0.46f, font * 22.0f);
        at = {element_right ? margin : io.DisplaySize.x - margin, margin};
        pivot = {element_right ? 0.0f : 1.0f, 0.0f};
    }
    ImGui::SetNextWindowPos(at, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowSizeConstraints({width, 0.0f}, {width, io.DisplaySize.y - 2.0f * margin});
    ImGui::Begin("##touch_editor", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize);
    bool done = back;
    if (st.selected) {
        const Element e = *st.selected;
        input::touch::Placement &p = layout.at(e);
        heading(input::touch::info(e).name);
        if (input::touch::rebindable(e)) {
            const auto &choices = input::touch::button_choices();
            int current = 0;
            for (std::size_t i = 0; i < choices.size(); ++i)
                if (choices[i].buttons == p.buttons) current = static_cast<int>(i);
            if (const int delta = choice_row("Presses", input::touch::buttons_label(p.buttons),
                    {false, {}, "The PSP button, or two pressed in the same frame."})) {
                p.buttons =
                    choices[static_cast<std::size_t>(cycle(current, delta, static_cast<int>(choices.size())))].buttons;
                settings::save();
            }
        }
        const float normal = input::touch::default_action_layout().at(e).size;
        int size = static_cast<int>(std::lround(p.size / normal * 100.0f));
        if (slider_row("Size", size, 50, 250, 5, "%d%%", {false, {}, "Of the element's own default size."})) {
            p.size = std::clamp(normal * static_cast<float>(size) / 100.0f, input::touch::kMinElementSize,
                input::touch::kMaxElementSize);
            settings::save();
        }
        RowOptions shown{false, {}, "A hidden element is not drawn and does not take fingers."};
        if (e == Element::Pause) {
            shown.disabled = true;
            shown.note = "It opens this menu";
        }
        if (toggle_row("Shown", p.shown, shown)) {
            p.shown = !p.shown;
            settings::save();
        }
        if (button_row("Reset this element", {false, {}, "Back to where and what it was at first."})) {
            p = input::touch::default_action_layout().at(e);
            settings::save();
        }
        if (button_row("Choose another", {false, {}, "Or tap another element."})) st.selected.reset();
    } else {
        heading("Touch layout");
        paragraph("Drag an element to move it; tap one to resize, rebind or hide it.", colors::kTextDim);
        int opacity = static_cast<int>(std::lround(s.touch_opacity * 100.0f));
        if (slider_row("Opacity", opacity, 10, 100, 5, "%d%%", {false, {}, "How strongly the controls are drawn."})) {
            s.touch_opacity = static_cast<float>(opacity) / 100.0f;
            settings::save();
        }
        int all = static_cast<int>(std::lround(s.touch_size * 100.0f));
        if (slider_row("Size of all", all, 60, 160, 5, "%d%%", {false, {}, "Every element together."})) {
            s.touch_size = static_cast<float>(all) / 100.0f;
            settings::save();
        }
        if (toggle_row("Haptic feedback", s.touch_haptics,
                {false, {},
                    "A short vibration when a button is pressed or a swipe is taken, where the "
                    "device can (the system's touch feedback setting applies)."})) {
            s.touch_haptics = !s.touch_haptics;
            settings::save();
        }
        if (choice_row("Panel", st.panel_bottom ? "Bottom" : "Top", {false, {}, "Where this panel sits."}))
            st.panel_bottom = !st.panel_bottom;
        if (button_row(
                "Reset the whole layout", {false, {}, "Every element back to where and what it was at first."})) {
            layout = input::touch::default_action_layout();
            settings::save();
        }
    }
    if (button_row("Done", {false, {}, "Back to the menu."})) done = true;
    ImGui::End();
    if (done) {
        st = State{};
        settings::save();
    }
}

} // namespace mhp3rd::ui
