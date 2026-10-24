#include "ui/bindings_editor.hpp"

#include "ui/layer.hpp"
#include "ui/widgets.hpp"

#include "gpu/vulkan_renderer.hpp"
#include "input/bindings.hpp"
#include "input/presets.hpp"
#include "settings/settings.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mhp3rd::ui {
namespace {

using input::Action;

float font() {
    return Layer::get().font_size();
}
float px(float value) {
    return std::round(value * Layer::get().scale());
}

// How the menu labels the gamepad's inputs: as the connected pad does.
input::PadStyle pad_style() {
    SDL_Gamepad *pad = Layer::get().renderer().gamepad();
    if (pad == nullptr) return input::PadStyle::Xbox;
    switch (SDL_GetGamepadType(pad)) {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
        return input::PadStyle::PlayStation;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return input::PadStyle::Nintendo;
    default:
        return input::PadStyle::Xbox;
    }
}

// The confirm setting swaps the pad's bottom and right buttons for every
// binding (see the renderer): what a binding names and the button pressed
// for it differ by that swap, both ways.
input::Binding confirm_swap(input::Binding binding) {
    if (!settings::current().confirm_south) return binding;
    if (binding == input::pad(input::PadInput::South)) return input::pad(input::PadInput::East);
    if (binding == input::pad(input::PadInput::East)) return input::pad(input::PadInput::South);
    return binding;
}
input::Chord confirm_swap(input::Chord chord) {
    for (input::Binding &b : chord.inputs)
        if (b != input::kNone) b = confirm_swap(b);
    return chord;
}

// A chord as the player presses it.
std::string chord_label(const input::Chord &chord, bool pad, input::PadStyle style) {
    return input::label(pad ? confirm_swap(chord) : chord, style);
}
std::string binding_label(input::Binding binding, bool pad, input::PadStyle style) {
    return input::label(pad ? confirm_swap(binding) : binding, style);
}

std::string action_name(Action action) {
    std::string label = input::info(action).label;
    // "○  (confirm)" is ○ here; the role says the rest.
    if (const auto cut = label.find("  ("); cut != std::string::npos) label.resize(cut);
    return label;
}

// A target of the layout (input::Table): an action, or one of the player's
// combinations, named by its buttons.
using Target = std::size_t;
constexpr Target target_of(Action action) {
    return static_cast<Target>(action);
}
bool is_combo(Target target) {
    return target >= input::kActions;
}
std::string target_name(Target target) {
    if (!is_combo(target)) return action_name(static_cast<Action>(target));
    const std::vector<input::Combo> &combos = settings::current().controls.combos;
    const std::size_t n = target - input::kActions;
    if (n >= combos.size()) return {};
    const std::string buttons = input::buttons_label(combos[n].buttons);
    return buttons.empty() ? std::string("New combination") : buttons;
}

// What an action does in the game, next to its name.
const char *role(Action action) {
    switch (action) {
    case Action::Triangle:
        return "Attack";
    case Action::Circle:
        return "Second attack, confirm";
    case Action::TriangleCircle:
        return "Both at once";
    case Action::R:
        return "Guard, run, aim";
    case Action::Cross:
        return "Evade, back";
    case Action::Square:
        return "Use item, sheathe";
    case Action::L:
        return "Item bar with □ ○";
    case Action::ItemLeft:
        return "L + □ in one press";
    case Action::ItemRight:
        return "L + ○ in one press";
    case Action::Start:
        return "Pause";
    default:
        return "";
    }
}

const char *device_name(bool pad) {
    return pad ? "gamepad" : "keyboard";
}

struct Capture {
    Target action{};
    bool pad{};
    std::size_t slot{};
    bool adding{};
};

struct State {
    std::optional<Capture> capture;
    // Conflicts the player chose to keep, for this session.
    std::set<std::string> kept;
    // A word about the last capture, under its action's row.
    std::optional<Target> note_action;
    std::string note;
    // What had the focus, this frame and the last.
    BindingsFocus focus{BindingsFocus::None};
    bool focus_resettable{};
    std::optional<Target> focus_action;
    std::optional<Target> last_focus_action;
    // The combination whose buttons are being chosen, if any.
    std::optional<std::size_t> picking;
    // After an edit, which can move rows about: scroll the focused action's
    // row back into view.
    int reveal{}; // frames left
    // After a conflict's line goes away with its buttons: the focus moves to
    // the action's first chip on that device.
    std::optional<std::pair<Target, bool>> focus_request;
};

State &state() {
    static State value;
    return value;
}

// Changes the layout in use, if `change` does: a shipped preset first
// becomes a preset of the player's, which then keeps the change.
template <class Change> bool edit(std::string &notice, Change &&change) {
    settings::Settings &s = settings::current();
    input::Layout next = s.controls;
    if (!change(next)) return false;
    if (const std::optional<std::string> made = settings::prepare_controls_edit(s))
        notice = "Your change is in a new preset of your own, " + *made + ". The shipped presets stay as they are.";
    s.controls = next;
    settings::controls_edited(s);
    settings::save();
    state().reveal = 3;
    return true;
}

void finish_capture(std::string &notice) {
    State &st = state();
    Layer &layer = Layer::get();
    if (!st.capture) return;
    const std::optional<input::Chord> pressed = layer.take_captured_binding();
    if (!pressed) {
        if (!layer.capturing_binding()) st.capture.reset();
        return;
    }
    const Capture c = *st.capture;
    st.capture.reset();
    st.reveal = 3;
    if (pressed->empty()) return;
    const input::Chord chord = c.pad ? confirm_swap(*pressed) : *pressed;
    if (is_combo(c.action) && c.action - input::kActions >= settings::current().controls.combos.size()) return;
    const input::Slots &slots = input::slots(settings::current().controls, c.pad, c.action);
    const auto same = std::find(slots.begin(), slots.end(), chord);
    if (same != slots.end() && (c.adding || static_cast<std::size_t>(same - slots.begin()) != c.slot)) {
        st.note_action = c.action;
        st.note = chord_label(chord, c.pad, pad_style()) + " is bound to " + target_name(c.action) +
            " already; nothing changed.";
        return;
    }
    if (!input::valid(chord)) {
        st.note_action = c.action;
        st.note = "A combination is up to four inputs on one device; nothing changed.";
        return;
    }
    st.note_action.reset();
    edit(notice, [&](input::Layout &layout) {
        input::Slots &b = input::slots(layout, c.pad, c.action);
        return c.adding ? input::add(b, chord) : input::replace(b, c.slot, chord);
    });
}

void start_capture(Target action, bool pad, std::size_t slot, bool adding) {
    Layer &layer = Layer::get();
    if (layer.capturing_binding()) return;
    state().capture = Capture{action, pad, slot, adding};
    state().note_action.reset();
    layer.begin_binding_capture(pad ? Layer::Capture::Pad : Layer::Capture::Keys);
}

struct Columns {
    float left{};   // the row's left edge
    float width{};  // the whole row
    float label{};  // the action's name
    float keys{};   // the keyboard and mouse column
    float pad{};    // the gamepad column
    float column{}; // the width of each
    float reset{};  // the reset chip
};

Columns columns() {
    Columns c;
    c.left = ImGui::GetCursorScreenPos().x;
    c.width = ImGui::GetContentRegionAvail().x;
    const float gap = font() * 0.5f;
    const float reset = std::round(font() * 1.8f);
    c.label = std::round(c.width * 0.27f);
    c.column = std::floor((c.width - c.label - reset - gap * 3.0f) * 0.5f);
    c.keys = c.left + c.label;
    c.pad = c.keys + c.column + gap;
    c.reset = c.left + c.width - reset - gap * 0.5f;
    return c;
}

float chip_height() {
    return std::round(font() * 1.45f);
}
float clear_width() {
    return std::round(font() * 1.05f);
}

// The group's title, with the columns' names over them.
void group_header(input::ActionGroup group, const Columns &c, bool first) {
    ImGui::Dummy({0.0f, px(first ? 6.0f : 14.0f)});
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const float size = ImGui::GetStyle().FontSizeBase * 0.8f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImFont *f = ImGui::GetFont();
    draw->AddText(f, size, {at.x + px(16.0f), at.y}, colors::kAccent, input::group_name(group));
    draw->AddText(f, size, {c.keys + px(4.0f), at.y}, colors::kTextDim, "Keyboard and mouse");
    draw->AddText(f, size, {c.pad + px(4.0f), at.y}, colors::kTextDim, "Gamepad");
    ImGui::Dummy({0.0f, size + px(4.0f)});
}

// The × in a binding's chip.
void draw_cross(ImDrawList *draw, ImVec2 center, float size, ImU32 color) {
    const float h = size * 0.5f;
    const float thickness = std::max(1.5f, size * 0.16f);
    draw->AddLine({center.x - h, center.y - h}, {center.x + h, center.y + h}, color, thickness);
    draw->AddLine({center.x - h, center.y + h}, {center.x + h, center.y - h}, color, thickness);
}

// A circular arrow, for reset.
void draw_reset_icon(ImDrawList *draw, ImVec2 center, float radius, ImU32 color) {
    const float thickness = std::max(1.5f, radius * 0.22f);
    const float start = -IM_PI * 0.35f;
    const float end = IM_PI * 1.35f;
    draw->PathArcTo(center, radius, start, end, 20);
    draw->PathStroke(color, 0, thickness);
    const ImVec2 tip{center.x + std::cos(start) * radius, center.y + std::sin(start) * radius};
    const float a = radius * 0.55f;
    draw->AddTriangleFilled({tip.x - a, tip.y - a * 0.2f}, {tip.x + a * 0.6f, tip.y - a * 0.9f},
        {tip.x + a * 0.35f, tip.y + a * 0.6f}, color);
}

enum class ChipKind { Binding, Add, Reset, Fix };

struct ChipResult {
    bool pressed{};
    bool clear{};
    bool focused{};
    bool hovered{};
};

// One chip: a focusable, clickable pill. Binding chips have a × at the right
// that clears them, as do a right click, Delete and the pad's top button.
ChipResult chip(const char *id, ImVec2 at, float width, const std::string &text, ChipKind kind, bool warning, bool live,
    bool locked) {
    const float height = chip_height();
    ImGui::SetCursorScreenPos(at);
    ChipResult r;
    const bool pressed = ImGui::InvisibleButton(id, {width, height}, ImGuiButtonFlags_EnableNav);
    r.focused = ImGui::IsItemFocused();
    r.hovered = ImGui::IsItemHovered();
    const ImGuiIO &io = ImGui::GetIO();
    const float clear_x = at.x + width - clear_width();
    if (kind == ChipKind::Binding && !locked) {
        const bool on_cross = pressed && io.MouseReleased[0] && io.MousePos.x >= clear_x;
        const bool right_click = r.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        const bool key = r.focused && !io.WantTextInput &&
            (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false) || ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
                ImGui::IsKeyPressed(ImGuiKey_Backspace, false));
        r.clear = on_cross || right_click || key;
        r.pressed = pressed && !on_cross;
    } else {
        r.pressed = pressed && !locked;
    }

    ImDrawList *draw = ImGui::GetWindowDrawList();
    const ImVec2 min = at;
    const ImVec2 max{at.x + width, at.y + height};
    const float rounding = height * 0.3f;
    const bool hot = r.focused || r.hovered;
    ImU32 fill = kind == ChipKind::Add ? IM_COL32(0, 0, 0, 0) : colors::kRow;
    if (live)
        fill = colors::kAccent;
    else if (r.focused)
        fill = colors::kRowFocus;
    else if (r.hovered)
        fill = colors::kRowHover;
    ImU32 edge = warning ? colors::kDanger : kind == ChipKind::Binding ? colors::kPanelEdge : colors::kTextDisabled;
    if (r.focused) edge = colors::kAccentBright;
    draw->AddRectFilled(min, max, fill, rounding);
    draw->AddRect(min, max, edge, rounding, 0, r.focused ? px(2.5f) : px(1.5f));
    if (r.focused && !live)
        draw->AddRect({min.x - px(3.0f), min.y - px(3.0f)}, {max.x + px(3.0f), max.y + px(3.0f)},
            IM_COL32(251, 230, 166, 90), rounding + px(3.0f), 0, px(1.5f));

    ImU32 ink = locked ? colors::kTextDisabled : warning ? colors::kDanger : colors::kText;
    if (kind == ChipKind::Add && !hot) ink = colors::kTextDim;
    if (live) ink = colors::kPanel;
    const float text_right = kind == ChipKind::Binding ? clear_x : max.x;
    const ImVec2 size = ImGui::CalcTextSize(text.c_str());
    const float text_x = kind == ChipKind::Binding ? min.x + font() * 0.45f : min.x + (width - size.x) * 0.5f;
    if (kind == ChipKind::Reset) {
        draw_reset_icon(draw, {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f}, height * 0.26f,
            hot ? colors::kAccentBright : colors::kAccent);
    } else {
        draw->PushClipRect(min, {text_right, max.y}, true);
        draw->AddText({text_x, min.y + (height - size.y) * 0.5f}, ink, text.c_str());
        draw->PopClipRect();
    }
    if (kind == ChipKind::Binding && !locked && !live) {
        const bool over_cross = r.hovered && io.MousePos.x >= clear_x;
        draw_cross(draw, {clear_x + clear_width() * 0.4f, (min.y + max.y) * 0.5f}, font() * 0.36f,
            over_cross ? colors::kDanger
                : hot  ? colors::kText
                       : colors::kTextDim);
    }
    return r;
}

float chip_width(const std::string &text, ChipKind kind) {
    const float text_width = ImGui::CalcTextSize(text.c_str()).x;
    switch (kind) {
    case ChipKind::Binding:
        return std::round(text_width + font() * 0.45f + font() * 0.3f + clear_width());
    case ChipKind::Add:
        return std::round(std::max(chip_height() * 1.3f, text_width + font() * 1.0f));
    case ChipKind::Reset:
        return chip_height() * 1.2f;
    case ChipKind::Fix:
        return std::round(text_width + font() * 1.0f);
    }
    return text_width;
}

struct Placed {
    std::string text;
    ChipKind kind{};
    std::size_t slot{};
    ImVec2 at;
    float width{};
};

// The chips of one device's bindings, flowed onto as many lines as they need.
std::vector<Placed> place(
    const input::Slots &slots, bool pad, float left, float width, float top, input::PadStyle style, int &lines) {
    std::vector<Placed> placed;
    const std::size_t count = input::count(slots);
    for (std::size_t i = 0; i < count; ++i)
        placed.push_back({chord_label(slots[i], pad, style), ChipKind::Binding, i, {}, 0.0f});
    if (count < input::kSlots) placed.push_back({count == 0u ? "+  Add" : "+", ChipKind::Add, count, {}, 0.0f});
    const float gap = px(8.0f);
    const float line = chip_height() + px(6.0f);
    float x = left;
    lines = 1;
    for (Placed &p : placed) {
        p.width = std::min(chip_width(p.text, p.kind), width);
        if (x > left && x + p.width > left + width) {
            x = left;
            ++lines;
        }
        p.at = {x, top + line * static_cast<float>(lines - 1)};
        x += p.width + gap;
    }
    return placed;
}

std::string conflict_key(bool pad, Target action, const input::Conflict &c) {
    std::string mine = std::to_string(static_cast<int>(action)) + ":" + input::format(c.chord);
    std::string theirs = std::to_string(static_cast<int>(c.other)) + ":" + input::format(c.theirs);
    if (theirs < mine) std::swap(mine, theirs);
    return std::string(pad ? "pad " : "keys ") + mine + " | " + theirs;
}

// What an action has on both devices differs from its preset's default. A
// combination of the player's has no default.
bool differs(const settings::Settings &s, Target action) {
    if (is_combo(action)) return false;
    const input::Layout &base = input::layout(settings::base_preset(s));
    const auto i = static_cast<std::size_t>(action);
    return s.controls.keys[i] != base.keys[i] || s.controls.pad[i] != base.pad[i];
}

std::string slots_text(const input::Slots &slots, bool pad, input::PadStyle style) {
    std::string text;
    for (std::size_t i = 0; i < input::count(slots); ++i)
        text += (text.empty() ? "" : ", ") + chord_label(slots[i], pad, style);
    return text.empty() ? std::string("nothing") : text;
}

void reset_action(Target action, std::string &notice) {
    const input::Preset base = settings::base_preset(settings::current());
    edit(notice, [&](input::Layout &layout) {
        const auto i = static_cast<std::size_t>(action);
        const input::Layout &from = input::layout(base);
        if (layout.keys[i] == from.keys[i] && layout.pad[i] == from.pad[i]) return false;
        layout.keys[i] = from.keys[i];
        layout.pad[i] = from.pad[i];
        return true;
    });
    state().note_action.reset();
}

// The lines under an action for its conflicts, each with its fix.
void conflict_lines(Target action, const Columns &c, bool locked, input::PadStyle style, std::string &notice) {
    State &st = state();
    settings::Settings &s = settings::current();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const float size = ImGui::GetStyle().FontSizeBase * 0.85f;
    ImFont *f = ImGui::GetFont();
    int index = 0;
    for (const bool pad : {false, true}) {
        for (const input::Conflict &conflict : input::conflicts(input::table(s.controls, pad), action)) {
            const std::string key = conflict_key(pad, action, conflict);
            if (st.kept.count(key) != 0u) continue;
            ImGui::PushID(index++);
            const std::string other = target_name(conflict.other);
            const std::string mine = chord_label(conflict.chord, pad, style);
            const std::string theirs = chord_label(conflict.theirs, pad, style);
            const std::string window =
                s.chord_window != 0u ? " waits up to " + std::to_string(s.chord_window) + " ms for the rest, then" : "";
            std::string text;
            std::string fix = "Remove " + theirs + " from " + other;
            switch (conflict.kind) {
            case input::Conflict::Kind::Same:
                text = mine + " also presses " + other + ".";
                fix = "Remove from " + other;
                break;
            case input::Conflict::Kind::Part:
                text = theirs + " (" + other + ") is part of " + mine + ": pressed alone it" + window + " does " +
                    other + "; with the rest, only " + target_name(action) + ".";
                break;
            case input::Conflict::Kind::Contains:
                text = mine + " is part of " + theirs + " (" + other + "): pressed alone it" + window + " does " +
                    target_name(action) + "; with the rest, only " + other + ".";
                break;
            case input::Conflict::Kind::Held:
                text = theirs + " holds " + other + ", which the game reads together with other buttons; while " +
                    mine + " is held it does only " + target_name(action) + ", and " + other + " lets go.";
                break;
            }
            text = std::string(pad ? "Gamepad: " : "Keyboard: ") + text;
            const float keep_width = chip_width("Keep both", ChipKind::Fix);
            const float fix_width = chip_width(fix, ChipKind::Fix);
            const float gap = px(8.0f);
            const float buttons = keep_width + fix_width + gap;
            const float text_left = c.keys + px(4.0f);
            const float text_width = std::max(font() * 4.0f, c.left + c.width - buttons - gap * 2.0f - text_left);
            const ImVec2 text_size = f->CalcTextSizeA(size, FLT_MAX, text_width, text.c_str());
            const float height = std::max(chip_height(), text_size.y) + px(4.0f);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            // A warning sign in the label column.
            const ImVec2 sign{c.keys - font() * 0.8f, at.y + height * 0.5f};
            draw->AddCircleFilled(sign, font() * 0.36f, colors::kDanger);
            const ImVec2 bang = f->CalcTextSizeA(size, FLT_MAX, 0.0f, "!");
            draw->AddText(f, size, {sign.x - bang.x * 0.5f, sign.y - bang.y * 0.5f}, colors::kPanel, "!");
            draw->AddText(f, size, {text_left, at.y + (height - text_size.y) * 0.5f}, colors::kDanger, text.c_str(),
                nullptr, text_width);
            const float y = at.y + (height - chip_height()) * 0.5f;
            const float right = c.left + c.width - gap;
            const ChipResult keep =
                chip("keep", {right - keep_width, y}, keep_width, "Keep both", ChipKind::Fix, false, false, locked);
            const ChipResult remove = chip(
                "fix", {right - keep_width - gap - fix_width, y}, fix_width, fix, ChipKind::Fix, false, false, locked);
            if (keep.focused || remove.focused) {
                st.focus = BindingsFocus::Fix;
                st.focus_action = action;
            }
            if (keep.focused || keep.hovered)
                Layer::get().set_description("Keep both: one press does both. The warning goes away until Yakumo "
                                             "starts again.");
            if (remove.focused || remove.hovered)
                Layer::get().set_description("Takes " + chord_label(conflict.theirs, pad, style) + " away from " +
                    other + " on the " + device_name(pad) + ", so it does only " + target_name(action) + ".");
            if (keep.pressed || remove.pressed) st.focus_request = std::make_pair(action, pad);
            if (keep.pressed) st.kept.insert(key);
            if (remove.pressed) {
                const input::Conflict fixed = conflict;
                edit(notice, [&](input::Layout &layout) {
                    return input::remove(input::slots(layout, pad, fixed.other), fixed.theirs);
                });
            }
            ImGui::SetCursorScreenPos({at.x, at.y + height});
            ImGui::Dummy({0.0f, 0.0f});
            ImGui::PopID();
        }
    }
}

// The chip in a combination's name column: its buttons, which it opens
// for choosing.
bool combo_name_chip(Target action, const Columns &c, float y, bool locked, const std::string &name) {
    const float width = std::min(chip_width(name, ChipKind::Fix), c.keys - c.left - px(24.0f));
    const bool open = state().picking == action - input::kActions;
    const ChipResult r = chip("name", {c.left + px(16.0f), y}, width, name, ChipKind::Fix, false, open, locked);
    if (r.focused) {
        state().focus = BindingsFocus::Fix;
        state().focus_action = action;
    }
    if (r.focused || r.hovered)
        Layer::get().set_description("The PSP buttons this combination presses together. Select it to choose them.");
    return r.pressed;
}

// A combination's PSP buttons, each a chip that turns on and off, under its
// row while it is open.
void button_picker(Target action, const Columns &c, bool locked, std::string &notice) {
    State &st = state();
    const std::size_t n = action - input::kActions;
    const std::uint32_t buttons = settings::current().controls.combos[n].buttons;
    static constexpr std::uint32_t kOrder[] = {
        0x1000u, 0x2000u, 0x4000u, 0x8000u, 0x0100u, 0x0200u, 0x0010u, 0x0040u, 0x0080u, 0x0020u, 0x0008u, 0x0001u};
    const float gap = px(8.0f);
    const float line = chip_height() + px(6.0f);
    const float left = c.keys;
    const float right = c.left + c.width - gap;
    ImVec2 at = ImGui::GetCursorScreenPos();
    const float top = at.y;
    float x = left;
    float y = top;
    ImGui::PushID("picker");
    for (const std::uint32_t bit : kOrder) {
        const std::string text = input::buttons_label(bit);
        const float width = std::max(chip_width(text, ChipKind::Fix), chip_height() * 1.4f);
        if (x > left && x + width > right) {
            x = left;
            y += line;
        }
        ImGui::PushID(static_cast<int>(bit));
        const ChipResult r = chip("button", {x, y}, width, text, ChipKind::Fix, false, (buttons & bit) != 0u, locked);
        if (r.focused) {
            st.focus = BindingsFocus::Fix;
            st.focus_action = action;
        }
        if (r.focused || r.hovered)
            Layer::get().set_description("Presses " + text +
                " with the others that are on. The game sees them "
                "all in the same frame, L and R one frame ahead, as a player holds them.");
        if (r.pressed)
            edit(notice, [&](input::Layout &layout) {
                if (n >= layout.combos.size()) return false;
                layout.combos[n].buttons ^= bit;
                return true;
            });
        ImGui::PopID();
        x += width + gap;
    }
    {
        const std::string text = "Done";
        const float width = chip_width(text, ChipKind::Fix);
        if (x > left && x + width > right) {
            x = left;
            y += line;
        }
        const ChipResult r = chip("done", {x, y}, width, text, ChipKind::Fix, false, false, false);
        if (r.focused) {
            st.focus = BindingsFocus::Fix;
            st.focus_action = action;
        }
        if (r.pressed) st.picking.reset();
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos({at.x, y + line});
    ImGui::Dummy({0.0f, 0.0f});
}

void action_row(Target action, const Columns &c, bool locked, input::PadStyle style, std::string &notice) {
    State &st = state();
    Layer &layer = Layer::get();
    settings::Settings &s = settings::current();
    const auto i = static_cast<std::size_t>(action);
    const bool combo = is_combo(action);
    ImGui::PushID(static_cast<int>(i));
    const ImVec2 top = ImGui::GetCursorScreenPos();
    const float pad_y = px(3.0f);
    int key_lines = 1;
    int pad_lines = 1;
    const std::vector<Placed> keys =
        place(input::slots(s.controls, false, action), false, c.keys, c.column, top.y + pad_y, style, key_lines);
    const std::vector<Placed> pads =
        place(input::slots(s.controls, true, action), true, c.pad, c.column, top.y + pad_y, style, pad_lines);
    const int lines = std::max(key_lines, pad_lines);
    const float height =
        chip_height() * static_cast<float>(lines) + px(6.0f) * static_cast<float>(lines - 1) + pad_y * 2.0f;
    const ImVec2 bottom{top.x + c.width, top.y + height};

    // The row with the focus stands out, as the menu's rows do.
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const bool row_focused = st.last_focus_action == action;
    if (row_focused) {
        draw->AddRectFilled(top, bottom, IM_COL32(217, 166, 75, 26), px(6.0f));
        draw->AddRectFilled(top, {top.x + px(4.0f), bottom.y}, colors::kAccent, px(6.0f), ImDrawFlags_RoundCornersLeft);
    }
    const std::vector<input::Conflict> key_clashes = input::conflicts(input::table(s.controls, false), action);
    const std::vector<input::Conflict> pad_clashes = input::conflicts(input::table(s.controls, true), action);
    const auto clashes = [&](bool pad, const input::Chord &chord) {
        for (const input::Conflict &conflict : pad ? pad_clashes : key_clashes)
            if (conflict.chord == chord && st.kept.count(conflict_key(pad, action, conflict)) == 0u) return true;
        return false;
    };

    // The name, and what it does; a combination's name is its buttons, a
    // chip that opens them.
    const float text_y = top.y + pad_y + (chip_height() - font()) * 0.5f;
    const std::string name = target_name(action);
    if (combo) {
        if (combo_name_chip(action, c, top.y + pad_y, locked, name)) {
            const std::size_t n = action - input::kActions;
            if (st.picking == n)
                st.picking.reset();
            else
                st.picking = n;
        }
    } else {
        draw->PushClipRect(top, {c.keys - px(8.0f), bottom.y}, true);
        draw->AddText({top.x + px(16.0f), text_y}, row_focused ? colors::kAccentBright : colors::kText, name.c_str());
        if (const char *what = role(static_cast<Action>(action)); *what != '\0') {
            const float size = ImGui::GetStyle().FontSizeBase * 0.78f;
            const float x = top.x + px(16.0f) + ImGui::CalcTextSize(name.c_str()).x + font() * 0.6f;
            draw->AddText(ImGui::GetFont(), size, {x, text_y + font() * 0.14f}, colors::kTextDim, what);
        }
        draw->PopClipRect();
    }

    const bool capturing = st.capture && st.capture->action == action;
    for (const bool pad : {false, true}) {
        ImGui::PushID(pad ? "pad" : "keys");
        const input::Slots &slots = input::slots(s.controls, pad, action);
        for (const Placed &p : pad ? pads : keys) {
            ImGui::PushID(static_cast<int>(p.slot));
            const bool live = capturing && st.capture->pad == pad && st.capture->slot == p.slot;
            const bool warning = p.kind == ChipKind::Binding && clashes(pad, slots[p.slot]);
            const ChipResult r =
                chip("chip", p.at, p.width, live ? std::string("…") : p.text, p.kind, warning, live, locked);
            if (p.slot == 0u && st.focus_request == std::make_pair(action, pad)) {
                st.focus_request.reset();
                ImGui::FocusItem();
            }
            if (r.focused) {
                st.focus = p.kind == ChipKind::Binding ? BindingsFocus::Binding : BindingsFocus::Add;
                st.focus_action = action;
            }
            if (r.focused || r.hovered) {
                std::string description;
                if (p.kind == ChipKind::Binding)
                    description = p.text + " on the " + device_name(pad) + " presses " + name +
                        ". Select it to press something else in its place; clear it with its ×.";
                else
                    description = std::string("Add a binding on the ") + device_name(pad) + ": press " +
                        (pad ? "a button" : "a key or a mouse button") +
                        ", or up to four together in any order for a combination.";
                if (locked) description += std::string("\nSet by ") + settings::overridden_by("input.preset");
                layer.set_description(description);
            }
            if (!locked) {
                if (r.clear) {
                    const std::size_t slot = p.slot;
                    edit(notice,
                        [&](input::Layout &layout) { return input::clear(input::slots(layout, pad, action), slot); });
                    st.note_action.reset();
                } else if (r.pressed) {
                    start_capture(action, pad, p.slot, p.kind == ChipKind::Add);
                }
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    // Reset, when there is something to reset; a combination of the
    // player's is removed there instead.
    const bool resettable = differs(s, action);
    if (st.focus_action == action) st.focus_resettable = resettable;
    if (combo) {
        const float width = chip_width("", ChipKind::Reset);
        const ChipResult r = chip("remove", {c.reset, top.y + pad_y}, width, "×", ChipKind::Fix, true, false, locked);
        if (r.focused) {
            st.focus = BindingsFocus::Fix;
            st.focus_action = action;
        }
        if (r.focused || r.hovered) layer.set_description("Removes this combination and its bindings.");
        if (r.pressed) {
            const std::size_t n = action - input::kActions;
            edit(notice, [&](input::Layout &layout) {
                if (n >= layout.combos.size()) return false;
                layout.combos.erase(layout.combos.begin() + static_cast<std::ptrdiff_t>(n));
                return true;
            });
            st.picking.reset();
            st.kept.clear();
        }
    } else if (resettable) {
        const float width = chip_width("", ChipKind::Reset);
        const ChipResult r = chip("reset", {c.reset, top.y + pad_y}, width, "", ChipKind::Reset, false, false, locked);
        if (r.focused) {
            st.focus = BindingsFocus::Reset;
            st.focus_action = action;
            st.focus_resettable = true;
        }
        if (r.focused || r.hovered) {
            const input::Preset base = settings::base_preset(s);
            const input::Layout &from = input::layout(base);
            layer.set_description("Back to " + std::string(input::info(base).name) + ": " +
                slots_text(from.keys[i], false, style) + " on the keyboard, " + slots_text(from.pad[i], true, style) +
                " on the gamepad.");
        }
        if (r.pressed) reset_action(action, notice);
    }
    // The pad's Select (View) button resets the focused action from any chip.
    // Not a face button: ImGui's navigation keeps the left one.
    if (!locked && st.focus_action == action && resettable && ImGui::IsKeyPressed(ImGuiKey_GamepadBack, false))
        reset_action(action, notice);

    ImGui::SetCursorScreenPos({top.x, bottom.y});
    ImGui::Dummy({0.0f, 0.0f});

    if (combo && st.picking == action - input::kActions) button_picker(action, c, locked, notice);
    if (st.note_action == action && !st.note.empty()) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float size = ImGui::GetStyle().FontSizeBase * 0.85f;
        draw->AddText(ImGui::GetFont(), size, {c.keys + px(4.0f), at.y}, colors::kTextDim, st.note.c_str());
        ImGui::Dummy({0.0f, size + px(4.0f)});
    }
    conflict_lines(action, c, locked, style, notice);
    if (st.reveal > 0 && st.last_focus_action == action) {
        --st.reveal;
        const ImRect row(top, {bottom.x, ImGui::GetCursorScreenPos().y});
        ImGui::ScrollToRect(ImGui::GetCurrentWindow(), row, ImGuiScrollFlags_KeepVisibleEdgeY);
    }
    ImGui::PopID();
}

// The player's own combinations (#198), after the actions, and a chip that
// makes a new one.
void combo_rows(const Columns &c, bool locked, input::PadStyle style, std::string &notice) {
    State &st = state();
    settings::Settings &s = settings::current();
    ImGui::Dummy({0.0f, px(14.0f)});
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const float size = ImGui::GetStyle().FontSizeBase * 0.8f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    draw->AddText(ImGui::GetFont(), size, {at.x + px(16.0f), at.y}, colors::kAccent, "Your combinations");
    if (!s.controls.combos.empty()) {
        draw->AddText(ImGui::GetFont(), size, {c.keys + px(4.0f), at.y}, colors::kTextDim, "Keyboard and mouse");
        draw->AddText(ImGui::GetFont(), size, {c.pad + px(4.0f), at.y}, colors::kTextDim, "Gamepad");
    }
    ImGui::Dummy({0.0f, size + px(4.0f)});
    ImGui::PushID("combos");
    for (std::size_t n = 0; n < s.controls.combos.size(); ++n)
        action_row(input::kActions + n, c, locked, style, notice);
    if (st.picking && *st.picking >= s.controls.combos.size()) st.picking.reset();
    const std::string text = "+  New combination";
    const float width = chip_width(text, ChipKind::Add);
    const ImVec2 top = ImGui::GetCursorScreenPos();
    const bool full = s.controls.combos.size() >= input::kMaxCombos;
    const ChipResult r =
        chip("new", {c.left + px(16.0f), top.y + px(3.0f)}, width, text, ChipKind::Add, false, false, locked || full);
    if (r.focused) {
        st.focus = BindingsFocus::Add;
        st.focus_action.reset();
    }
    if (r.focused || r.hovered)
        Layer::get().set_description(full ? "At most " + std::to_string(input::kMaxCombos) + " combinations."
                                          : std::string("An action of your own that presses any PSP buttons "
                                                        "together, such as × + ○. Choose its buttons, then bind it "
                                                        "like any action."));
    if (r.pressed && !full)
        edit(notice, [&](input::Layout &layout) {
            layout.combos.push_back({});
            st.picking = layout.combos.size() - 1u;
            return true;
        });
    ImGui::SetCursorScreenPos({top.x, top.y + chip_height() + px(6.0f)});
    ImGui::Dummy({0.0f, 0.0f});
    ImGui::PopID();
}

} // namespace

void bindings_editor(std::string &notice) {
    State &st = state();
    settings::Settings &s = settings::current();
    finish_capture(notice);
    st.last_focus_action = st.focus_action;
    st.focus = BindingsFocus::None;
    st.focus_resettable = false;
    st.focus_action.reset();
    const char *locked_by = settings::overridden_by("input.preset");
    const bool locked = locked_by != nullptr;
    const input::PadStyle style = pad_style();
    const Columns c = columns();
    // Nothing had the focus to reveal.
    if (!st.last_focus_action) st.reveal = 0;
    ImGui::PushID("bindings");
    bool first = true;
    for (std::size_t g = 0; g < input::kActionGroups; ++g) {
        const auto group = static_cast<input::ActionGroup>(g);
        std::vector<Action> actions;
        for (std::size_t i = 0; i < input::kActions; ++i)
            if (input::group_of(static_cast<Action>(i)) == group) actions.push_back(static_cast<Action>(i));
        if (actions.empty()) continue;
        group_header(group, c, first);
        first = false;
        for (const Action action : actions) action_row(target_of(action), c, locked, style, notice);
        if (group == input::ActionGroup::Movement) {
            RowOptions o;
            o.description = "Which stick moves the hunter; the other one is the camera stick. Part of the preset.";
            if (locked) {
                o.disabled = true;
                o.note = std::string("Set by ") + locked_by;
            }
            if (toggle_row("Move with the right stick", s.controls.swap_sticks, o))
                edit(notice, [](input::Layout &layout) {
                    layout.swap_sticks = !layout.swap_sticks;
                    return true;
                });
        }
    }
    combo_rows(c, locked, style, notice);
    ImGui::PopID();
    const float size = ImGui::GetStyle().FontSizeBase * 0.8f;
    ImGui::Dummy({0.0f, px(4.0f)});
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), size, {at.x + px(16.0f), at.y}, colors::kTextDim,
        "Always: Esc, or L3 + R3 on a gamepad, opens this menu; F3 shows the "
        "performance overlay.");
    ImGui::Dummy({0.0f, size + px(6.0f)});
}

BindingsFocus bindings_focus() {
    return state().focus;
}

std::string bindings_summary(input::Action action) {
    const settings::Settings &s = settings::current();
    const auto i = static_cast<std::size_t>(action);
    if (i >= input::kActions) return {};
    const input::PadStyle style = pad_style();
    const auto line = [&](const input::Slots &slots, bool pad) {
        std::string text;
        for (std::size_t n = 0; n < input::count(slots); ++n)
            text += (text.empty() ? "" : " / ") + chord_label(slots[n], pad, style);
        return text;
    };
    const std::string keys = line(s.controls.keys[i], false);
    const std::string pad = line(s.controls.pad[i], true);
    if (keys.empty() || pad.empty()) return keys + pad;
    return keys + "; " + pad;
}

std::size_t bindings_conflicts() {
    const settings::Settings &s = settings::current();
    std::size_t count = 0;
    for (const bool pad : {false, true})
        for (std::size_t i = 0; i < input::kActions + s.controls.combos.size(); ++i) {
            for (const input::Conflict &c : input::conflicts(input::table(s.controls, pad), i))
                if (state().kept.count(conflict_key(pad, i, c)) == 0u) {
                    ++count;
                    break;
                }
        }
    return count;
}

bool bindings_focus_resettable() {
    return state().focus_resettable;
}

void bindings_capture_prompt() {
    State &st = state();
    Layer &layer = Layer::get();
    if (!st.capture || !layer.capturing_binding()) return;
    const Capture &c = *st.capture;
    const input::PadStyle style = pad_style();
    const ImGuiIO &io = ImGui::GetIO();
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    ImFont *f = ImGui::GetFont();
    const float base = ImGui::GetStyle().FontSizeBase;
    draw->AddRectFilled({0.0f, 0.0f}, io.DisplaySize, IM_COL32(8, 5, 3, 150));

    const bool pad = c.pad;
    const settings::Settings &s = settings::current();
    if (is_combo(c.action) && c.action - input::kActions >= s.controls.combos.size()) return;
    const input::Slots &slots = input::slots(s.controls, pad, c.action);
    std::string held;
    for (const input::Binding b : layer.capture_held()) held += (held.empty() ? "" : " + ") + input::label(b, style);
    const std::string title = std::string(pad ? "Gamepad" : "Keyboard and mouse") + "   ·   " + target_name(c.action);
    const std::string what = c.adding ? (input::count(slots) == 0u ? "New binding" : "Another binding")
                                      : "In place of " + chord_label(slots[c.slot], pad, style);
    const std::string big = !held.empty() ? held + (layer.capture_held().size() < input::kChordInputs ? "  + …" : "")
                                          : (pad ? "Press a button" : "Press a key or a mouse button");
    const std::string example = pad
        ? input::label(
              input::chord(input::pad(input::PadInput::LeftShoulder), input::pad(input::PadInput::East)), style) +
            " or " +
            input::label(input::chord(input::pad(input::PadInput::North), input::pad(input::PadInput::East)), style)
        : std::string("Left Shift + F");
    const std::string combination = "Up to four together, in any order, for a combination, such as " + example +
        ". It is kept when you let go of all of them.";
    std::string cancel;
    if (pad) {
        cancel = "Hold " + input::label(layer.pad_back_button(), style) + " to cancel   ·   Esc or a touch cancels";
        if (layer.capture_held().empty())
            cancel += "   ·   gives up in " + std::to_string(layer.capture_seconds_left()) + " s";
    } else {
        cancel = "Esc, a gamepad button or a touch cancels";
    }

    const float width = std::min(io.DisplaySize.x - font() * 2.0f, font() * 28.0f);
    const float padding = font() * 1.1f;
    const float inner = width - padding * 2.0f;
    struct Line {
        const std::string *text;
        float size;
        ImU32 color;
        float space;
    };
    const Line lines[] = {
        {&title, base * 1.05f, colors::kAccentBright, 0.0f},
        {&what, base * 0.85f, colors::kTextDim, px(2.0f)},
        {&big, base * 1.6f, colors::kText, font() * 0.9f},
        {&combination, base * 0.85f, colors::kTextDim, font() * 0.9f},
        {&cancel, base * 0.85f, colors::kTextDim, font() * 0.5f},
    };
    float height = padding * 2.0f;
    for (const Line &line : lines)
        height += line.space + f->CalcTextSizeA(line.size, FLT_MAX, inner, line.text->c_str()).y;
    const float bar = pad ? px(6.0f) + font() * 0.5f : 0.0f;
    height += bar;
    const ImVec2 min{std::round((io.DisplaySize.x - width) * 0.5f), std::round((io.DisplaySize.y - height) * 0.5f)};
    const ImVec2 max{min.x + width, min.y + height};
    draw->AddRectFilled(min, max, colors::kPanel, px(12.0f));
    draw->AddRect(min, max, colors::kAccent, px(12.0f), 0, px(2.0f));
    float y = min.y + padding;
    for (const Line &line : lines) {
        y += line.space;
        const ImVec2 size = f->CalcTextSizeA(line.size, FLT_MAX, inner, line.text->c_str());
        const bool centred = line.text == &big;
        const float x = centred ? min.x + (width - size.x) * 0.5f : min.x + padding;
        draw->AddText(f, line.size, {x, y}, line.color, line.text->c_str(), nullptr, inner);
        y += size.y;
    }
    if (pad) {
        // How far holding the back button has come towards cancelling.
        y += font() * 0.5f;
        const float progress = layer.capture_cancel_progress();
        draw->AddRectFilled({min.x + padding, y}, {max.x - padding, y + px(6.0f)}, colors::kTrack, px(3.0f));
        if (progress > 0.0f)
            draw->AddRectFilled(
                {min.x + padding, y}, {min.x + padding + inner * progress, y + px(6.0f)}, colors::kDanger, px(3.0f));
    }
}

} // namespace mhp3rd::ui
