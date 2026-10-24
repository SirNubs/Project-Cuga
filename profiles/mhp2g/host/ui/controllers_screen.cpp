// Controls > Controllers (#147). The list of controllers and their live
// state, and the setup for a controller SDL has no mapping for: it asks for
// each PSP control in turn, reading the device as a plain joystick, and saves
// the answers as an SDL mapping (input/gamepad_mapping.hpp).

#include "ui/controllers_screen.hpp"

#include "ui/layer.hpp"
#include "ui/ui.hpp"
#include "ui/widgets.hpp"

#include "gpu/vulkan_renderer.hpp"
#include "input/gamepad_devices.hpp"
#include "input/gamepad_mapping.hpp"
#include "install/user_data.hpp"

#include "imgui.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mhp2g::ui {
namespace {

using Clock = std::chrono::steady_clock;
namespace mapping = input::mapping;
namespace devices = input::devices;
using mapping::Target;

float px(float value) {
    return std::round(value * Layer::get().scale());
}
float font() {
    return Layer::get().font_size();
}

// What the setup asks for, in order, and how. The home button is left out:
// nothing in the port uses it, and a PS2 pad's Analog button reports nothing.
struct Step {
    Target target;
    const char *name;   // in the summary
    const char *prompt; // what to do
};
constexpr Step kSteps[] = {
    {Target::A, "× (bottom)", "Press the bottom face button: × on a PlayStation pad, A on an Xbox pad."},
    {Target::B, "○ (right)", "Press the right face button: ○ on a PlayStation pad, B on an Xbox pad."},
    {Target::X, "□ (left)", "Press the left face button: □ on a PlayStation pad, X on an Xbox pad."},
    {Target::Y, "△ (top)", "Press the top face button: △ on a PlayStation pad, Y on an Xbox pad."},
    {Target::DpadUp, "D-pad up", "Press up on the D-pad."},
    {Target::DpadDown, "D-pad down", "Press down on the D-pad."},
    {Target::DpadLeft, "D-pad left", "Press left on the D-pad."},
    {Target::DpadRight, "D-pad right", "Press right on the D-pad."},
    {Target::LeftShoulder, "L1", "Press L1 (LB), the left shoulder button. This is the PSP's L."},
    {Target::RightShoulder, "R1", "Press R1 (RB), the right shoulder button. This is the PSP's R."},
    {Target::LeftTrigger, "L2", "Press L2 (LT), the left trigger."},
    {Target::RightTrigger, "R2", "Press R2 (RT), the right trigger."},
    {Target::Back, "SELECT", "Press SELECT (Back, View or Share)."},
    {Target::Start, "START", "Press START (Menu or Options)."},
    {Target::LeftStick, "L3", "Press the left stick in (L3)."},
    {Target::RightStick, "R3", "Press the right stick in (R3)."},
    {Target::LeftX, "Left stick, left and right", "Push the left stick all the way to the right, then let it go."},
    {Target::LeftY, "Left stick, up and down", "Push the left stick all the way down, then let it go."},
    {Target::RightX, "Right stick, left and right", "Push the right stick all the way to the right, then let it go."},
    {Target::RightY, "Right stick, up and down", "Push the right stick all the way down, then let it go."},
};
constexpr std::size_t kStepCount = std::size(kSteps);

const char *step_name(Target target) {
    for (const Step &s : kSteps)
        if (s.target == target) return s.name;
    return mapping::field(target);
}

struct Wizard {
    enum class Phase { Settle, Ask, Release, Review };
    bool active{};
    SDL_JoystickID id{};
    std::string name;
    Phase phase{Phase::Settle};
    Clock::time_point settle_started{};
    std::size_t step{};
    std::size_t next{}; // the step after the release
    mapping::Answers answers{};
    mapping::Snapshot rest;
    std::vector<std::size_t> history; // the steps done, for going back
    std::string message;
};

struct ScreenState {
    bool open{};
    bool focus{}; // focus the first row next frame
    SDL_JoystickID selected{};
    Wizard wizard;
    std::string notice; // what the last save or removal did
};

ScreenState &screen() {
    static ScreenState value;
    return value;
}

std::string usb_ids(const devices::Info &i) {
    char text[16]{};
    std::snprintf(text, sizeof(text), "%04x:%04x", i.vendor, i.product);
    return text;
}

std::string inputs_text(const devices::Info &i) {
    return std::to_string(i.buttons) + (i.buttons == 1 ? " button, " : " buttons, ") + std::to_string(i.axes) +
        (i.axes == 1 ? " axis, " : " axes, ") + std::to_string(i.hats) + (i.hats == 1 ? " hat" : " hats");
}

// The mapping with room to wrap.
std::string spaced(const std::string &text) {
    std::string out;
    for (char c : text) {
        out += c;
        if (c == ',') out += ' ';
    }
    return out;
}

bool is_game_pad(SDL_JoystickID id) {
    SDL_Gamepad *pad = Layer::get().renderer().gamepad();
    return pad != nullptr && SDL_GetGamepadID(pad) == id;
}

std::string status_text(const devices::Info &i) {
    if (i.gamepad) return is_game_pad(i.id) ? "Gamepad, the game's" : "Gamepad";
    return devices::unmapped_gamepad(i) ? "Not set up" : "Joystick, not set up";
}

// A paragraph lined up with the rows' labels.
void text(const std::string &words, ImU32 color) {
    ImGui::Indent(px(16.0f));
    paragraph(words, color);
    ImGui::Unindent(px(16.0f));
}

// ---- Live state -----------------------------------------------------------

const char *hat_text(std::uint8_t hat) {
    switch (hat) {
    case SDL_HAT_CENTERED:
        return "centred";
    case SDL_HAT_UP:
        return "up";
    case SDL_HAT_RIGHT:
        return "right";
    case SDL_HAT_DOWN:
        return "down";
    case SDL_HAT_LEFT:
        return "left";
    case SDL_HAT_RIGHTUP:
        return "up right";
    case SDL_HAT_RIGHTDOWN:
        return "down right";
    case SDL_HAT_LEFTUP:
        return "up left";
    case SDL_HAT_LEFTDOWN:
        return "down left";
    default:
        return "?";
    }
}

// Every button as a numbered box, lit while held; every hat's direction;
// every axis as a bar from -1 to 1.
void draw_live(SDL_JoystickID id) {
    const mapping::Snapshot now = devices::snapshot(id);
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const float left = ImGui::GetCursorScreenPos().x + px(16.0f);
    const float width = ImGui::GetContentRegionAvail().x - px(32.0f);
    const float small = font() * 0.8f;

    if (!now.buttons.empty()) {
        const float box = std::round(font() * 1.5f);
        const float gap = px(6.0f);
        const int per_line = std::max(1, static_cast<int>((width + gap) / (box + gap)));
        const int lines = (static_cast<int>(now.buttons.size()) + per_line - 1) / per_line;
        const ImVec2 top{left, ImGui::GetCursorScreenPos().y + px(4.0f)};
        ImGui::Dummy({width, lines * (box + gap) + px(4.0f)});
        for (std::size_t k = 0; k < now.buttons.size(); ++k) {
            const int column = static_cast<int>(k) % per_line;
            const int line = static_cast<int>(k) / per_line;
            const ImVec2 min{top.x + column * (box + gap), top.y + line * (box + gap)};
            const ImVec2 max{min.x + box, min.y + box};
            const bool held = now.buttons[k];
            draw->AddRectFilled(min, max, held ? colors::kAccent : colors::kTrack, px(4.0f));
            const std::string number = std::to_string(k);
            const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(small, FLT_MAX, 0.0f, number.c_str());
            draw->AddText(nullptr, small, {min.x + (box - size.x) * 0.5f, min.y + (box - size.y) * 0.5f},
                held ? colors::kPanel : colors::kTextDim, number.c_str());
        }
    }
    for (std::size_t k = 0; k < now.hats.size(); ++k) {
        const std::string text = "Hat " + std::to_string(k) + ": " + hat_text(now.hats[k]);
        ImGui::SetCursorScreenPos({left, ImGui::GetCursorScreenPos().y});
        ImGui::PushStyleColor(ImGuiCol_Text, now.hats[k] != 0u ? colors::kAccentBright : colors::kTextDim);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopStyleColor();
    }
    for (std::size_t k = 0; k < now.axes.size(); ++k) {
        const float value = static_cast<float>(now.axes[k]) / 32767.0f;
        const float label_width = font() * 5.0f;
        const float bar_height = std::round(font() * 0.5f);
        const ImVec2 at{left, ImGui::GetCursorScreenPos().y + px(2.0f)};
        ImGui::Dummy({width, font() * 1.2f});
        char label[32]{};
        std::snprintf(label, sizeof(label), "Axis %zu  %+.2f", k, static_cast<double>(value));
        draw->AddText(nullptr, small, at, std::abs(value) > 0.5f ? colors::kAccentBright : colors::kTextDim, label);
        const ImVec2 bar_min{at.x + label_width, at.y + (font() - bar_height) * 0.5f};
        const ImVec2 bar_max{at.x + width, bar_min.y + bar_height};
        draw->AddRectFilled(bar_min, bar_max, colors::kTrack, bar_height * 0.5f);
        const float middle = (bar_min.x + bar_max.x) * 0.5f;
        const float reach = (bar_max.x - bar_min.x) * 0.5f * std::clamp(value, -1.0f, 1.0f);
        draw->AddRectFilled({std::min(middle, middle + reach), bar_min.y},
            {std::max(middle, middle + reach), bar_max.y}, colors::kAccent, bar_height * 0.5f);
        draw->AddLine({middle, bar_min.y - px(2.0f)}, {middle, bar_max.y + px(2.0f)}, colors::kTextDim, px(1.0f));
    }
    // What SDL makes of it as a gamepad.
    if (SDL_Gamepad *pad = SDL_IsGamepad(id) ? SDL_GetGamepadFromID(id) : nullptr) {
        std::string held;
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b)
            if (SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(b))) {
                if (!held.empty()) held += ", ";
                held += SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(b));
            }
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a) {
            const int value = SDL_GetGamepadAxis(pad, static_cast<SDL_GamepadAxis>(a));
            if (std::abs(value) < 16384) continue;
            if (!held.empty()) held += ", ";
            held += std::string(SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(a))) + (value > 0 ? "+" : "-");
        }
        ImGui::SetCursorScreenPos({left, ImGui::GetCursorScreenPos().y + px(4.0f)});
        ImGui::PushStyleColor(ImGuiCol_Text, held.empty() ? colors::kTextDim : colors::kAccentBright);
        ImGui::TextWrapped("As a gamepad: %s", held.empty() ? "nothing held" : held.c_str());
        ImGui::PopStyleColor();
    }
}

// ---- The setup --------------------------------------------------------------

void start_wizard(const devices::Info &i) {
    Wizard &w = screen().wizard;
    w = Wizard{};
    w.active = true;
    w.id = i.id;
    w.name = i.name;
    w.phase = Wizard::Phase::Settle;
    w.settle_started = Clock::now();
    screen().notice.clear();
    screen().focus = true;
    std::printf("[pad] setting up %s\n", i.name.c_str());
    std::fflush(stdout);
}

void finish_wizard() {
    screen().wizard.active = false;
    screen().focus = true;
    Layer::get().set_pad_blocked(false);
}

// Reads the device once a frame and moves the setup on.
void advance(Wizard &w) {
    const mapping::Snapshot now = devices::snapshot(w.id);
    switch (w.phase) {
    case Wizard::Phase::Settle: {
        // Whatever was held when the setup started (a button that opened it)
        // is let go of first; a button that never lets go counts as at rest.
        const bool idle = std::none_of(now.buttons.begin(), now.buttons.end(), [](bool b) { return b; }) &&
            std::all_of(now.hats.begin(), now.hats.end(), [](std::uint8_t h) { return h == 0u; });
        if (idle || Clock::now() - w.settle_started > std::chrono::milliseconds(1500)) {
            w.rest = now;
            w.phase = Wizard::Phase::Ask;
        }
        break;
    }
    case Wizard::Phase::Ask: {
        const Target target = kSteps[w.step].target;
        const std::optional<mapping::Element> found = mapping::detect(w.rest, now, mapping::is_axis(target));
        if (!found) break;
        for (std::size_t t = 0; t < mapping::kTargets; ++t) {
            if (static_cast<Target>(t) == target || !w.answers[t].same_input(*found)) continue;
            // A stick's axis may also be half of a D-pad on pads without a
            // hat; anything else answered twice is a slip.
            w.message = mapping::describe(*found) + " is " + step_name(static_cast<Target>(t)) +
                " already. Press another, or skip this one.";
            std::cout << "[pad] setup: " << w.message << std::endl;
            w.next = w.step;
            w.phase = Wizard::Phase::Release;
            return;
        }
        w.answers[static_cast<std::size_t>(target)] = *found;
        w.message = std::string(kSteps[w.step].name) + ": " + mapping::describe(*found);
        std::cout << "[pad] setup: " << w.message << std::endl;
        w.history.push_back(w.step);
        w.next = w.step + 1u;
        w.phase = Wizard::Phase::Release;
        break;
    }
    case Wizard::Phase::Release:
        if (!mapping::at_rest(w.rest, now)) break;
        w.step = w.next;
        w.phase = w.step >= kStepCount ? Wizard::Phase::Review : Wizard::Phase::Ask;
        if (w.phase == Wizard::Phase::Review) screen().focus = true;
        break;
    case Wizard::Phase::Review:
        break;
    }
}

void skip(Wizard &w) {
    w.answers[static_cast<std::size_t>(kSteps[w.step].target)] = {};
    w.history.push_back(w.step);
    w.message = std::string(kSteps[w.step].name) + ": skipped";
    std::cout << "[pad] setup: " << w.message << std::endl;
    w.step += 1u;
    w.phase = w.step >= kStepCount ? Wizard::Phase::Review : Wizard::Phase::Ask;
    if (w.phase == Wizard::Phase::Review) screen().focus = true;
}

void go_back(Wizard &w) {
    if (w.history.empty()) return;
    w.step = w.history.back();
    w.history.pop_back();
    w.answers[static_cast<std::size_t>(kSteps[w.step].target)] = {};
    w.message.clear();
    w.phase = Wizard::Phase::Ask;
}

void wizard_frame(bool back) {
    ScreenState &s = screen();
    Wizard &w = s.wizard;
    if (back) {
        std::printf("[pad] setup of %s cancelled\n", w.name.c_str());
        std::fflush(stdout);
        finish_wizard();
        return;
    }
    if (devices::joystick(w.id) == nullptr) {
        s.notice = w.name + " was disconnected during the setup.";
        finish_wizard();
        return;
    }
    // The pad being set up must not also move through this screen.
    Layer::get().set_pad_blocked(true);
    advance(w);

    section(("Setting up " + w.name).c_str());
    if (s.focus) {
        focus_next_row();
        s.focus = false;
    }
    if (w.phase != Wizard::Phase::Review) {
        const std::size_t shown = std::min(w.step, kStepCount - 1u);
        heading(w.phase == Wizard::Phase::Settle ? "Let go of the controller." : kSteps[shown].prompt);
        text("Step " + std::to_string(shown + 1u) + " of " + std::to_string(kStepCount) +
                ". Use the keyboard, the mouse or the touch screen here: the controller's own buttons are "
                "being recorded. Skip anything the controller does not have.",
            colors::kTextDim);
        if (!w.message.empty()) text(w.message, colors::kAccentBright);
        if (w.phase == Wizard::Phase::Release) text("Let go…", colors::kTextDim);
        if (button_row("Skip this one", {false, {}, "The controller has no such control, or it is not needed."}) &&
            w.phase == Wizard::Phase::Ask)
            skip(w);
        {
            RowOptions o{false, {}, "Asks for the previous control again."};
            o.disabled = w.history.empty();
            if (button_row("Back one step", o)) go_back(w);
        }
        if (button_row("Cancel", {false, {}, "Leaves the controller as it was. Esc does the same."})) {
            finish_wizard();
            return;
        }
    } else {
        heading("Done. Check the answers, then save.");
        const std::size_t answered = mapping::count(w.answers);
        if (button_row("Save and use this layout",
                {answered == 0u, answered == 0u ? "Nothing recorded" : "",
                    "Saves the layout to gamecontrollerdb.txt in the data folder and uses it at once: the "
                    "controller then drives the game and this menu."},
                colors::kAccentBright)) {
            const std::string error = devices::save_mapping(w.id, w.answers);
            s.notice = error.empty() ? w.name + " is set up. It now works in the game and in this menu." : error;
            finish_wizard();
            return;
        }
        if (button_row("Start over", {false, {}, "Asks for every control again."})) {
            const devices::Info again{w.id, w.name};
            start_wizard(again);
            return;
        }
        if (button_row("Cancel", {false, {}, "Leaves the controller as it was."})) {
            finish_wizard();
            return;
        }
    }
    section("Recorded");
    for (std::size_t k = 0; k < kStepCount; ++k) {
        const mapping::Element &e = w.answers[static_cast<std::size_t>(kSteps[k].target)];
        const bool done = std::find(w.history.begin(), w.history.end(), k) != w.history.end();
        const std::string value = !e.empty() ? mapping::describe(e) : done ? "skipped" : "—";
        ImGui::PushID(static_cast<int>(k));
        info_row(kSteps[k].name, value);
        ImGui::PopID();
    }
    section("The controller now");
    draw_live(w.id);
}

// ---- The list ---------------------------------------------------------------

void list_frame(bool back) {
    ScreenState &s = screen();
    if (back) {
        s.open = false;
        return;
    }
    const std::vector<devices::Info> all = devices::list();
    // The selection: kept while connected, else the first that needs setting
    // up, else the game's pad, else the first.
    const auto find = [&](SDL_JoystickID id) {
        return std::find_if(all.begin(), all.end(), [&](const devices::Info &i) { return i.id == id; });
    };
    if (find(s.selected) == all.end()) {
        s.selected = 0;
        for (const devices::Info &i : all)
            if (s.selected == 0 && devices::unmapped_gamepad(i)) s.selected = i.id;
        for (const devices::Info &i : all)
            if (s.selected == 0 && is_game_pad(i.id)) s.selected = i.id;
        if (s.selected == 0 && !all.empty()) s.selected = all.front().id;
    }

    section("Controllers");
    if (s.focus) {
        focus_next_row();
        s.focus = false;
    }
    if (all.empty()) {
        text("No controller is connected.", colors::kTextDim);
    }
    for (const devices::Info &i : all) {
        ImGui::PushID(static_cast<int>(i.id));
        if (list_row(
                "##device", i.name.empty() ? "(no name)" : i.name, status_text(i), ListIcon::None, i.id == s.selected))
            s.selected = i.id;
        ImGui::PopID();
    }
    if (!s.notice.empty()) text(s.notice, colors::kAccentBright);

    const auto chosen = find(s.selected);
    if (chosen != all.end()) {
        const devices::Info &i = *chosen;
        section(i.name.empty() ? "Controller" : i.name.c_str());
        if (!i.gamepad)
            text(devices::unmapped_gamepad(i)
                    ? "SDL has no layout for this controller, so neither the game nor this menu reads it. Set it "
                      "up: it takes a minute."
                    : "SDL has no layout for this device. If it is a controller, set it up.",
                colors::kDanger);
        if (button_row(i.gamepad ? "Set up this controller again" : "Set up this controller",
                {false, {},
                    "Asks for each button, the D-pad and the sticks in turn, then saves the layout "
                    "for this controller."},
                i.gamepad ? colors::kText : colors::kAccentBright)) {
            start_wizard(i);
            return;
        }
        if (i.saved &&
            button_row("Remove my layout",
                {false, {},
                    "Takes this controller's line out of gamecontrollerdb.txt: "
                    "it goes back to SDL's own layout, or to none at the next start."})) {
            const std::string error = devices::remove_mapping(i.id);
            s.notice = error.empty() ? "Your layout for " + i.name + " is removed." : error;
        }
        section("What it reports");
        text("Press anything on the controller to see it here.", colors::kTextDim);
        draw_live(i.id);
        section("Details");
        info_row("Status", status_text(i));
        info_row("USB vendor and product", usb_ids(i));
        info_row("GUID", i.guid);
        info_row("Inputs", inputs_text(i));
        info_row("Layout", !i.gamepad ? "None" : i.saved ? "Yours, from gamecontrollerdb.txt" : "SDL's own");
        if (i.gamepad) info_row("Mapping", spaced(i.mapping));
        if (button_row(
                "Copy these details", {false, {}, "Copies the name, USB ids, GUID and mapping, for a bug report."})) {
            const std::string details = "Controller: " + i.name + "\nUSB: " + usb_ids(i) + "\nGUID: " + i.guid +
                "\nInputs: " + inputs_text(i) + "\nMapping: " + (i.gamepad ? i.mapping : std::string("none")) +
                "\nPlatform: " + SDL_GetPlatform();
            SDL_SetClipboardText(details.c_str());
            s.notice = "Copied.";
        }
    }
    section("Layouts");
    text("Layouts you set up are kept in " + install::path_to_utf8(devices::mappings_file()) +
            ". Lines from the community's SDL_GameControllerDB can be pasted there too; they are read at "
            "start. SDL_GAMECONTROLLERCONFIG, if set, wins over both.",
        colors::kTextDim);
}

// ---- Notes --------------------------------------------------------------------

Clock::time_point &last_note() {
    static Clock::time_point value{};
    return value;
}

void note_for(SDL_JoystickID id, bool pressed) {
    const std::optional<devices::Info> i = devices::info(id);
    if (!i || !devices::unmapped_gamepad(*i)) return;
    const Clock::time_point now = Clock::now();
    if (pressed && now - last_note() < std::chrono::seconds(10)) return;
    last_note() = now;
    std::cout << "[pad] note: " << (i->name.empty() ? "a controller" : i->name) << " has no gamepad mapping"
              << std::endl;
    show_note("Unknown controller " + (i->name.empty() ? std::string() : "\"" + i->name + "\" ") +
        "does nothing yet: set it up in the menu (Esc), Controls > Controllers.");
}

} // namespace

void controllers_rows() {
    const std::vector<devices::Info> all = devices::list();
    const auto unknown =
        std::count_if(all.begin(), all.end(), [](const devices::Info &i) { return devices::unmapped_gamepad(i); });
    std::string value = all.empty() ? std::string("None") : std::to_string(all.size()) + " connected";
    if (unknown > 0) value += ", " + std::to_string(unknown) + " not set up";
    section("Controllers");
    RowOptions o;
    o.description = "Every controller connected, what each button reports, and a setup for a controller the game "
                    "does not know.";
    o.warning = unknown > 0;
    if (value_row("Connected controllers", value, o)) {
        screen().open = true;
        screen().focus = true;
        screen().notice.clear();
    }
}

bool controllers_screen_open() {
    return screen().open;
}

bool controllers_screen(bool back) {
    ScreenState &s = screen();
    if (!s.open) {
        if (s.wizard.active) finish_wizard();
        return false;
    }
    if (s.wizard.active)
        wizard_frame(back);
    else
        list_frame(back);
    if (!s.open) focus_next_row();
    return true;
}

void note_unknown_controller(const SDL_Event &event) {
    if (screen().wizard.active) return;
    if (event.type == SDL_EVENT_JOYSTICK_ADDED)
        note_for(event.jdevice.which, false);
    else if (event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN && !SDL_IsGamepad(event.jbutton.which))
        note_for(event.jbutton.which, true);
}

void note_unknown_controllers() {
    for (const devices::Info &i : devices::list())
        if (devices::unmapped_gamepad(i)) {
            note_for(i.id, false);
            break;
        }
}

} // namespace mhp2g::ui
