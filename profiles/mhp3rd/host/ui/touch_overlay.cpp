#include "ui/touch_overlay.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace mhp3rd::ui {
namespace {

using input::touch::Circle;
using input::touch::Control;

ImU32 colour(int r, int g, int b, float alpha) {
    return IM_COL32(r, g, b, static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
}

// The PSP's symbols drawn as shapes, so no font has to carry them.
void draw_symbol(ImDrawList *list, Control control, ImVec2 c, float r, ImU32 ink, float thickness) {
    const float s = r * 0.42f;
    switch (control) {
    case Control::Triangle:
        list->AddTriangle(
            {c.x, c.y - s}, {c.x + s * 0.95f, c.y + s * 0.65f}, {c.x - s * 0.95f, c.y + s * 0.65f}, ink, thickness);
        break;
    case Control::Circle:
        list->AddCircle(c, s * 0.9f, ink, 0, thickness);
        break;
    case Control::Cross:
        list->AddLine({c.x - s * 0.8f, c.y - s * 0.8f}, {c.x + s * 0.8f, c.y + s * 0.8f}, ink, thickness);
        list->AddLine({c.x + s * 0.8f, c.y - s * 0.8f}, {c.x - s * 0.8f, c.y + s * 0.8f}, ink, thickness);
        break;
    case Control::Square:
        list->AddRect({c.x - s * 0.75f, c.y - s * 0.75f}, {c.x + s * 0.75f, c.y + s * 0.75f}, ink, 0.0f, 0, thickness);
        break;
    case Control::Menu:
        for (int i = -1; i <= 1; ++i)
            list->AddLine({c.x - s * 0.9f, c.y + i * s * 0.6f}, {c.x + s * 0.9f, c.y + i * s * 0.6f}, ink, thickness);
        break;
    default:
        break;
    }
}

const char *label(Control control) {
    switch (control) {
    case Control::L:
        return "L";
    case Control::R:
        return "R";
    case Control::Start:
        return "START";
    case Control::Select:
        return "SELECT";
    default:
        return nullptr;
    }
}

using input::touch::Element;

ImVec2 add(ImVec2 a, ImVec2 b, float k = 1.0f) {
    return {a.x + b.x * k, a.y + b.y * k};
}

// A blade from the guard at `guard` along `d` (unit), `length` long, with its
// grip behind: the attack's icon, and twice for the combined attack.
void blade(ImDrawList *list, ImVec2 guard, ImVec2 d, float length, ImU32 ink, float t) {
    const ImVec2 p{-d.y, d.x};
    const float w = length * 0.13f;
    const ImVec2 tip = add(guard, d, length);
    const ImVec2 shoulder = add(guard, d, length * 0.72f);
    const ImVec2 points[] = {
        add(guard, p, w), add(shoulder, p, w * 0.8f), tip, add(shoulder, p, -w * 0.8f), add(guard, p, -w)};
    list->AddConvexPolyFilled(points, 5, ink);
    list->AddLine(add(guard, p, w * 3.0f), add(guard, p, -w * 3.0f), ink, t);
    const ImVec2 end = add(guard, d, -length * 0.42f);
    list->AddLine(guard, end, ink, t * 1.3f);
    list->AddCircleFilled(add(end, d, -t * 0.6f), t * 1.1f, ink);
}

// An arc from angle a0 to a1 (radians, y down) with an arrowhead at a1.
void arrow_arc(ImDrawList *list, ImVec2 c, float radius, float a0, float a1, ImU32 ink, float t) {
    list->PathArcTo(c, radius, a0, a1, 24);
    list->PathStroke(ink, 0, t);
    const ImVec2 end{c.x + std::cos(a1) * radius, c.y + std::sin(a1) * radius};
    const float direction = a1 > a0 ? 1.0f : -1.0f;
    const ImVec2 tangent{-std::sin(a1) * direction, std::cos(a1) * direction};
    const ImVec2 normal{std::cos(a1), std::sin(a1)};
    const float head = t * 2.2f;
    list->AddTriangleFilled(add(end, tangent, head * 1.1f), add(end, normal, head), add(end, normal, -head), ink);
}

// Each element's icon in a circle of radius r: our own simple shapes.
void draw_icon(ImDrawList *list, Element element, ImVec2 c, float r, ImU32 ink, float t) {
    const float s = r * 0.5f;
    constexpr float kRoot = 0.70710678f;
    switch (element) {
    case Element::Attack:
        blade(list, add(c, {-kRoot, kRoot}, s * 0.35f), {kRoot, -kRoot}, s * 1.35f, ink, t);
        break;
    case Element::Secondary:
        arrow_arc(list, c, s * 0.8f, 2.7f, 5.9f, ink, t * 1.3f);
        break;
    case Element::Combo:
        blade(list, add(c, {-kRoot, kRoot}, s * 0.45f), {kRoot, -kRoot}, s * 1.2f, ink, t);
        blade(list, add(c, {kRoot, kRoot}, s * 0.45f), {-kRoot, -kRoot}, s * 1.2f, ink, t);
        break;
    case Element::Evade:
        arrow_arc(list, c, s * 0.72f, -1.2f, 3.9f, ink, t * 1.3f);
        break;
    case Element::Use: {
        list->AddLine({c.x, c.y - s * 0.8f}, {c.x, c.y + s * 0.15f}, ink, t * 1.2f);
        list->AddTriangleFilled(
            {c.x - s * 0.3f, c.y + s * 0.05f}, {c.x + s * 0.3f, c.y + s * 0.05f}, {c.x, c.y + s * 0.42f}, ink);
        const ImVec2 cup[] = {{c.x - s * 0.62f, c.y + s * 0.1f}, {c.x - s * 0.62f, c.y + s * 0.72f},
            {c.x + s * 0.62f, c.y + s * 0.72f}, {c.x + s * 0.62f, c.y + s * 0.1f}};
        list->AddPolyline(cup, 4, ink, 0, t);
        break;
    }
    case Element::Guard: {
        list->PathLineTo({c.x - s * 0.65f, c.y - s * 0.7f});
        list->PathLineTo({c.x + s * 0.65f, c.y - s * 0.7f});
        list->PathLineTo({c.x + s * 0.65f, c.y - s * 0.05f});
        list->PathBezierQuadraticCurveTo({c.x + s * 0.6f, c.y + s * 0.6f}, {c.x, c.y + s * 0.88f});
        list->PathBezierQuadraticCurveTo({c.x - s * 0.6f, c.y + s * 0.6f}, {c.x - s * 0.65f, c.y - s * 0.05f});
        list->PathStroke(ink, ImDrawFlags_Closed, t * 1.2f);
        list->AddLine({c.x, c.y - s * 0.45f}, {c.x, c.y + s * 0.5f}, ink, t);
        break;
    }
    case Element::Special:
        list->AddCircle(c, s * 0.55f, ink, 0, t);
        for (const ImVec2 d : {ImVec2{1, 0}, ImVec2{-1, 0}, ImVec2{0, 1}, ImVec2{0, -1}})
            list->AddLine(add(c, d, s * 0.3f), add(c, d, s * 0.9f), ink, t);
        list->AddCircleFilled(c, t * 0.9f, ink);
        break;
    case Element::Item: {
        list->AddCircle({c.x, c.y + s * 0.22f}, s * 0.55f, ink, 0, t * 1.1f);
        list->AddRectFilled({c.x - s * 0.28f, c.y - s * 0.46f}, {c.x + s * 0.28f, c.y - s * 0.3f}, ink, t * 0.4f);
        list->AddTriangleFilled({c.x - s * 0.1f, c.y - s * 0.44f}, {c.x - s * 0.45f, c.y - s * 0.82f},
            {c.x - s * 0.02f, c.y - s * 0.72f}, ink);
        list->AddTriangleFilled({c.x + s * 0.1f, c.y - s * 0.44f}, {c.x + s * 0.02f, c.y - s * 0.72f},
            {c.x + s * 0.45f, c.y - s * 0.82f}, ink);
        break;
    }
    case Element::DpadUp:
        list->AddTriangleFilled(
            {c.x, c.y - s * 0.6f}, {c.x + s * 0.62f, c.y + s * 0.42f}, {c.x - s * 0.62f, c.y + s * 0.42f}, ink);
        break;
    case Element::DpadDown:
        list->AddTriangleFilled(
            {c.x, c.y + s * 0.6f}, {c.x - s * 0.62f, c.y - s * 0.42f}, {c.x + s * 0.62f, c.y - s * 0.42f}, ink);
        break;
    case Element::Pause:
        for (int i = -1; i <= 1; ++i)
            list->AddLine({c.x - s * 0.7f, c.y + i * s * 0.45f}, {c.x + s * 0.7f, c.y + i * s * 0.45f}, ink, t);
        break;
    case Element::Start:
        list->AddTriangleFilled(
            {c.x - s * 0.4f, c.y - s * 0.6f}, {c.x + s * 0.62f, c.y}, {c.x - s * 0.4f, c.y + s * 0.6f}, ink);
        break;
    case Element::Select:
        list->AddRect({c.x - s * 0.7f, c.y - s * 0.62f}, {c.x + s * 0.25f, c.y + s * 0.3f}, ink, 0.0f, 0, t);
        list->AddRectFilled({c.x - s * 0.2f, c.y - s * 0.12f}, {c.x + s * 0.7f, c.y + s * 0.7f}, ink);
        break;
    default:
        break;
    }
}

} // namespace

void draw_action_element(ImDrawList *list, Element element, const input::touch::Placement &placement,
    const input::touch::Placed &placed, const input::touch::Point &stick_thumb, const ElementLook &look) {
    const float base = std::clamp(look.opacity, 0.1f, 1.0f) * (look.hidden ? 0.45f : 1.0f);
    const ImVec2 c{placed.centre.x, placed.centre.y};
    const float r = placed.radius;
    const float t = std::max(1.5f, r * 0.07f);
    const ImU32 white = colour(255, 255, 255, base * (look.held ? 1.0f : 0.85f));
    const ImU32 accent = colour(236, 190, 96, std::max(base, 0.8f));
    if (element == Element::Swipe) {
        const ImVec2 min{c.x - placed.half_width, c.y - r};
        const ImVec2 max{c.x + placed.half_width, c.y + r};
        list->AddRectFilled(min, max, colour(0, 0, 0, base * 0.12f), r * 0.5f);
        list->AddRect(min, max, colour(255, 255, 255, base * (look.selected ? 0.9f : 0.3f)), r * 0.5f, 0,
            std::max(1.0f, t * 0.6f));
        // Chevrons at both ends, the swiped side lit.
        for (int side : {-1, 1}) {
            const ImU32 ink = colour(255, 255, 255, base * (look.swipe == side ? 1.0f : 0.45f));
            for (int k = 0; k < 2; ++k) {
                const float x = c.x + side * (placed.half_width - r * (0.55f + k * 0.4f));
                list->AddLine({x - side * r * 0.18f, c.y - r * 0.32f}, {x + side * r * 0.12f, c.y}, ink, t);
                list->AddLine({x + side * r * 0.12f, c.y}, {x - side * r * 0.18f, c.y + r * 0.32f}, ink, t);
            }
        }
        if (look.selected) list->AddRect(min, max, accent, r * 0.5f, 0, t * 1.4f);
        return;
    }
    list->AddCircleFilled(c, r, colour(0, 0, 0, base * (look.held ? 0.55f : 0.32f)));
    // A dark edge under the light one keeps the outline visible on a bright picture.
    list->AddCircle(c, r + t, colour(0, 0, 0, base * 0.55f), 0, t);
    list->AddCircle(c, r, white, 0, t);
    if (element == Element::Stick) {
        // The ring, arrows inside it and the knob where the thumb is.
        const float s = r * 0.8f;
        for (const ImVec2 d : {ImVec2{1, 0}, ImVec2{-1, 0}, ImVec2{0, 1}, ImVec2{0, -1}}) {
            const ImVec2 tip = add(c, d, s);
            const ImVec2 p{-d.y, d.x};
            list->AddTriangleFilled(tip, add(add(tip, d, -r * 0.14f), p, r * 0.1f),
                add(add(tip, d, -r * 0.14f), p, -r * 0.1f), colour(255, 255, 255, base * 0.6f));
        }
        ImVec2 knob = c;
        if (look.held) {
            const float dx = stick_thumb.x - c.x;
            const float dy = stick_thumb.y - c.y;
            const float length = std::hypot(dx, dy);
            const float k = length > r ? r / length : 1.0f;
            knob = {c.x + dx * k, c.y + dy * k};
        }
        list->AddCircleFilled(knob, r * 0.42f, colour(255, 255, 255, base * (look.held ? 0.75f : 0.45f)));
        list->AddCircle(knob, r * 0.42f, colour(0, 0, 0, base * 0.4f), 0, t);
    } else {
        if (look.held) list->AddCircleFilled(c, r * 0.94f, colour(255, 255, 255, std::max(base, 0.5f) * 0.55f));
        draw_icon(list, element, c, r, white, t);
        // What it presses, small beside it.
        // The icons of the D-pad and Start and Select say what they press;
        // the others', or one rebound, need the PSP's name beside them.
        const bool plain = element == Element::DpadUp || element == Element::DpadDown || element == Element::Start ||
            element == Element::Select;
        if (input::touch::rebindable(element) &&
            (!plain || placement.buttons != input::touch::default_action_layout().at(element).buttons)) {
            const std::string text = input::touch::buttons_label(placement.buttons);
            ImFont *font = ImGui::GetFont();
            const float size = std::max(12.0f, r * 0.42f);
            const ImVec2 extent = font->CalcTextSizeA(size, 1e9f, 0.0f, text.c_str());
            const ImVec2 at{c.x + r * 0.72f - extent.x * 0.3f, c.y + r * 0.62f};
            list->AddText(font, size, {at.x + 1.0f, at.y + 1.0f}, colour(0, 0, 0, base * 0.8f), text.c_str());
            list->AddText(font, size, at, colour(255, 255, 255, base * 0.9f), text.c_str());
        }
    }
    if (look.hidden) list->AddLine({c.x - r * 0.8f, c.y + r * 0.8f}, {c.x + r * 0.8f, c.y - r * 0.8f}, white, t);
    if (look.selected) list->AddCircle(c, r + t * 3.0f, accent, 0, t * 1.5f);
}

void draw_action_controls(const input::touch::ActionControls &controls, float opacity, std::uint64_t ms) {
    ImDrawList *list = ImGui::GetBackgroundDrawList();
    const input::touch::ActionLayout &layout = controls.layout();
    for (std::size_t i = 0; i < input::touch::kElements; ++i) {
        const auto element = static_cast<Element>(i);
        const input::touch::Placement &placement = layout.elements[i];
        if (!placement.shown) continue;
        ElementLook look;
        look.opacity = opacity;
        look.held = controls.held(element);
        if (element == Element::Swipe) look.swipe = controls.swipe_shown(ms);
        draw_action_element(list, element, placement, controls.placed(element), controls.stick_thumb(), look);
    }
}

void draw_touch_controls(const input::touch::Controls &controls, float opacity) {
    ImDrawList *list = ImGui::GetBackgroundDrawList();
    const input::touch::Layout &layout = controls.layout();
    const float base = std::clamp(opacity, 0.1f, 1.0f);
    // PSP colours for the symbols.
    const ImU32 symbol_colours[] = {colour(64, 224, 176, base), colour(240, 96, 112, base), colour(128, 160, 255, base),
        colour(232, 144, 208, base)};
    for (std::size_t i = 0; i < input::touch::kControls; ++i) {
        const auto control = static_cast<Control>(i);
        const Circle &circle = layout.controls[i];
        const ImVec2 c{circle.centre.x, circle.centre.y};
        const bool held = controls.held(control);
        const float thickness = std::max(1.5f, circle.radius * 0.08f);
        list->AddCircleFilled(c, circle.radius, colour(0, 0, 0, base * (held ? 0.55f : 0.3f)));
        // A dark edge under the light one keeps the outline visible on a bright picture.
        list->AddCircle(c, circle.radius + thickness, colour(0, 0, 0, base * 0.6f), 0, thickness);
        list->AddCircle(c, circle.radius, colour(255, 255, 255, base * (held ? 1.0f : 0.8f)), 0, thickness);
        if (held) list->AddCircleFilled(c, circle.radius * 0.92f, colour(255, 255, 255, base * 0.35f));
        const ImU32 ink = i < 4u ? symbol_colours[i] : colour(255, 255, 255, base);
        if (const char *text = label(control)) {
            ImFont *font = ImGui::GetFont();
            const float size = circle.radius * (text[1] == '\0' ? 0.9f : 0.42f);
            const ImVec2 extent = font->CalcTextSizeA(size, 1e9f, 0.0f, text);
            list->AddText(font, size, {c.x - extent.x * 0.5f, c.y - extent.y * 0.5f}, ink, text);
        } else {
            draw_symbol(list, control, c, circle.radius, ink, thickness * 1.3f);
        }
    }
    // The D-pad: a cross of four arms, each lit while held.
    if (layout.dpad_shown) {
        const ImVec2 c{layout.dpad.centre.x, layout.dpad.centre.y};
        const float reach = layout.dpad.radius;
        const float half = reach * 0.31f;
        const float thickness = std::max(1.5f, reach * 0.035f);
        const std::uint16_t held = controls.dpad_held();
        struct Arm {
            std::uint16_t bit;
            float dx, dy;
        };
        const Arm arms[] = {{0x10u, 0.0f, -1.0f}, {0x20u, 1.0f, 0.0f}, {0x40u, 0.0f, 1.0f}, {0x80u, -1.0f, 0.0f}};
        list->AddRectFilled({c.x - half, c.y - half}, {c.x + half, c.y + half}, colour(0, 0, 0, base * 0.3f));
        for (const Arm &arm : arms) {
            const bool on = (held & arm.bit) != 0u;
            // The arm's rectangle from the centre square out to the reach.
            const ImVec2 a{
                c.x + (arm.dx != 0.0f ? arm.dx * half : -half), c.y + (arm.dy != 0.0f ? arm.dy * half : -half)};
            const ImVec2 b{
                c.x + (arm.dx != 0.0f ? arm.dx * reach : half), c.y + (arm.dy != 0.0f ? arm.dy * reach : half)};
            const ImVec2 low{std::min(a.x, b.x), std::min(a.y, b.y)};
            const ImVec2 high{std::max(a.x, b.x), std::max(a.y, b.y)};
            list->AddRectFilled(low, high, colour(0, 0, 0, base * (on ? 0.55f : 0.3f)), half * 0.25f);
            if (on) list->AddRectFilled(low, high, colour(255, 255, 255, base * 0.8f), half * 0.25f);
            list->AddRect({low.x - thickness, low.y - thickness}, {high.x + thickness, high.y + thickness},
                colour(0, 0, 0, base * 0.6f), half * 0.25f, 0, thickness);
            list->AddRect(low, high, colour(255, 255, 255, base * (on ? 1.0f : 0.8f)), half * 0.25f, 0, thickness);
            // A small triangle pointing out along the arm.
            const float t = reach * 0.66f;
            const float w = half * 0.55f;
            const ImVec2 tip{c.x + arm.dx * (t + w * 0.6f), c.y + arm.dy * (t + w * 0.6f)};
            const ImVec2 side1{c.x + arm.dx * (t - w * 0.4f) - arm.dy * w, c.y + arm.dy * (t - w * 0.4f) + arm.dx * w};
            const ImVec2 side2{c.x + arm.dx * (t - w * 0.4f) + arm.dy * w, c.y + arm.dy * (t - w * 0.4f) - arm.dx * w};
            list->AddTriangleFilled(tip, side1, side2, colour(255, 255, 255, base * (on ? 1.0f : 0.8f)));
        }
    }
    // The floating stick while the thumb is down: its reach and the knob.
    const input::touch::Stick &stick = controls.stick_state();
    if (stick.active) {
        const ImVec2 origin{stick.origin.x, stick.origin.y};
        list->AddCircleFilled(origin, layout.stick_radius, colour(0, 0, 0, base * 0.3f));
        list->AddCircle(origin, layout.stick_radius, colour(255, 255, 255, base * 0.7f), 0,
            std::max(1.5f, layout.stick_radius * 0.04f));
        const input::touch::Point push = controls.stick();
        const ImVec2 knob{origin.x + push.x * layout.stick_radius, origin.y + push.y * layout.stick_radius};
        list->AddCircleFilled(knob, layout.stick_radius * 0.45f, colour(255, 255, 255, base * 0.6f));
    }
}

} // namespace mhp3rd::ui
