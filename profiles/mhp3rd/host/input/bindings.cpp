#include "input/bindings.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <utility>

namespace mhp3rd::input {
namespace {

struct KeyName {
    std::uint16_t position;
    const char *name;
};

// Names as SDL gives them, except Enter and Esc, which is what the keys say,
// and the keypad's symbols, spelled out so no name holds the separators
// " / " and " + ".
constexpr KeyName kKeyNames[] = {
    {4, "A"},
    {5, "B"},
    {6, "C"},
    {7, "D"},
    {8, "E"},
    {9, "F"},
    {10, "G"},
    {11, "H"},
    {12, "I"},
    {13, "J"},
    {14, "K"},
    {15, "L"},
    {16, "M"},
    {17, "N"},
    {18, "O"},
    {19, "P"},
    {20, "Q"},
    {21, "R"},
    {22, "S"},
    {23, "T"},
    {24, "U"},
    {25, "V"},
    {26, "W"},
    {27, "X"},
    {28, "Y"},
    {29, "Z"},
    {30, "1"},
    {31, "2"},
    {32, "3"},
    {33, "4"},
    {34, "5"},
    {35, "6"},
    {36, "7"},
    {37, "8"},
    {38, "9"},
    {39, "0"},
    {40, "Enter"},
    {41, "Esc"},
    {42, "Backspace"},
    {43, "Tab"},
    {44, "Space"},
    {45, "-"},
    {46, "="},
    {47, "["},
    {48, "]"},
    {49, "\\"},
    {50, "#"},
    {51, ";"},
    {52, "'"},
    {53, "`"},
    {54, ","},
    {55, "."},
    {56, "/"},
    {57, "CapsLock"},
    {58, "F1"},
    {59, "F2"},
    {60, "F3"},
    {61, "F4"},
    {62, "F5"},
    {63, "F6"},
    {64, "F7"},
    {65, "F8"},
    {66, "F9"},
    {67, "F10"},
    {68, "F11"},
    {69, "F12"},
    {70, "PrintScreen"},
    {71, "ScrollLock"},
    {72, "Pause"},
    {73, "Insert"},
    {74, "Home"},
    {75, "PageUp"},
    {76, "Delete"},
    {77, "End"},
    {78, "PageDown"},
    {79, "Right"},
    {80, "Left"},
    {81, "Down"},
    {82, "Up"},
    {83, "Numlock"},
    {84, "Keypad Divide"},
    {85, "Keypad Multiply"},
    {86, "Keypad Minus"},
    {87, "Keypad Plus"},
    {88, "Keypad Enter"},
    {89, "Keypad 1"},
    {90, "Keypad 2"},
    {91, "Keypad 3"},
    {92, "Keypad 4"},
    {93, "Keypad 5"},
    {94, "Keypad 6"},
    {95, "Keypad 7"},
    {96, "Keypad 8"},
    {97, "Keypad 9"},
    {98, "Keypad 0"},
    {99, "Keypad Period"},
    {100, "NonUSBackslash"},
    {224, "Left Ctrl"},
    {225, "Left Shift"},
    {226, "Left Alt"},
    {227, "Left GUI"},
    {228, "Right Ctrl"},
    {229, "Right Shift"},
    {230, "Right Alt"},
    {231, "Right GUI"},
};
constexpr const char *kMouseNames[] = {"Mouse Left", "Mouse Middle", "Mouse Right", "Mouse 4", "Mouse 5"};
constexpr const char *kSeparator = " / ";
constexpr const char *kJoin = " + ";

struct PadName {
    PadInput input;
    const char *name; // settings.ini, after "Pad "
    const char *xbox;
    const char *playstation;
    const char *nintendo; // by place: Nintendo's A is on the right, where Xbox has B
};
constexpr PadName kPadNames[] = {
    {PadInput::South, "South", "A", "×", "B"},
    {PadInput::East, "East", "B", "○", "A"},
    {PadInput::West, "West", "X", "□", "Y"},
    {PadInput::North, "North", "Y", "△", "X"},
    {PadInput::Back, "Back", "View", "Share", "−"},
    {PadInput::Guide, "Guide", "Guide", "PS", "Home"},
    {PadInput::Start, "Start", "Menu", "Options", "+"},
    {PadInput::LeftStick, "L3", "LS", "L3", "LS"},
    {PadInput::RightStick, "R3", "RS", "R3", "RS"},
    {PadInput::LeftShoulder, "LB", "LB", "L1", "L"},
    {PadInput::RightShoulder, "RB", "RB", "R1", "R"},
    {PadInput::DpadUp, "Up", "D-pad Up", "D-pad Up", "D-pad Up"},
    {PadInput::DpadDown, "Down", "D-pad Down", "D-pad Down", "D-pad Down"},
    {PadInput::DpadLeft, "Left", "D-pad Left", "D-pad Left", "D-pad Left"},
    {PadInput::DpadRight, "Right", "D-pad Right", "D-pad Right", "D-pad Right"},
    {PadInput::Misc1, "Misc", "Share", "Mute", "Capture"},
    {PadInput::RightPaddle1, "Paddle R1", "P1", "Paddle R1", "Paddle R1"},
    {PadInput::LeftPaddle1, "Paddle L1", "P3", "Paddle L1", "Paddle L1"},
    {PadInput::RightPaddle2, "Paddle R2", "P2", "Paddle R2", "Paddle R2"},
    {PadInput::LeftPaddle2, "Paddle L2", "P4", "Paddle L2", "Paddle L2"},
    {PadInput::Touchpad, "Touchpad", "Touchpad", "Touchpad", "Touchpad"},
    {PadInput::Misc2, "Misc 2", "Misc 2", "Misc 2", "Misc 2"},
    {PadInput::Misc3, "Misc 3", "Misc 3", "Misc 3", "Misc 3"},
    {PadInput::Misc4, "Misc 4", "Misc 4", "Misc 4", "Misc 4"},
    {PadInput::Misc5, "Misc 5", "Misc 5", "Misc 5", "Misc 5"},
    {PadInput::Misc6, "Misc 6", "Misc 6", "Misc 6", "Misc 6"},
    {PadInput::LeftTrigger, "LT", "LT", "L2", "ZL"},
    {PadInput::RightTrigger, "RT", "RT", "R2", "ZR"},
};
constexpr std::string_view kPadPrefix = "Pad ";

constexpr ActionInfo kInfo[kActions] = {
    {"stick_up", "Move forward"},
    {"stick_left", "Move left"},
    {"stick_down", "Move back"},
    {"stick_right", "Move right"},
    {"triangle", "△"},
    {"circle", "○  (confirm)"},
    {"cross", "×  (back)"},
    {"square", "□"},
    {"l", "L"},
    {"r", "R"},
    {"start", "START"},
    {"select", "SELECT"},
    {"dpad_up", "D-pad up"},
    {"dpad_left", "D-pad left"},
    {"dpad_down", "D-pad down"},
    {"dpad_right", "D-pad right"},
    {"camera_up", "Camera up"},
    {"camera_left", "Camera left"},
    {"camera_down", "Camera down"},
    {"camera_right", "Camera right"},
    {"triangle_circle", "△ + ○  (together)"},
    {"fast_forward", "Fast-forward"},
    {"screenshot", "Screenshot"},
    {"frame_step", "Frame step (photo mode)"},
    {"hide_hud", "Hide HUD"},
    {"item_left", "Item left  (L + □)"},
    {"item_right", "Item right  (L + ○)"},
    {"lock_on", "Lock on"},
};

// SceCtrlButtons for the actions that are buttons.
constexpr std::uint32_t kButtonBits[kActions] = {
    0u,
    0u,
    0u,
    0u,
    0x1000u,
    0x2000u,
    0x4000u,
    0x8000u,
    0x0100u,
    0x0200u,
    0x0008u,
    0x0001u,
    0x0010u,
    0x0080u,
    0x0040u,
    0x0020u,
    0u,
    0u,
    0u,
    0u,
    0x3000u,
    0u,
    0u,
    0u,
    0u,
    0x8100u,
    0x2100u,
    0u,
};

// The PSP's buttons as the menu and settings.ini name them, in the order
// they are listed.
struct ButtonName {
    std::uint32_t bit;
    const char *symbol; // the menu
    const char *name;   // settings.ini
};
constexpr ButtonName kButtonNames[] = {
    {0x1000u, "△", "Triangle"},
    {0x2000u, "○", "Circle"},
    {0x4000u, "×", "Cross"},
    {0x8000u, "□", "Square"},
    {0x0100u, "L", "L"},
    {0x0200u, "R", "R"},
    {0x0010u, "↑", "Up"},
    {0x0040u, "↓", "Down"},
    {0x0080u, "←", "Left"},
    {0x0020u, "→", "Right"},
    {0x0008u, "START", "Start"},
    {0x0001u, "SELECT", "Select"},
};

bool equal_ignoring_case(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return text;
}

const PadName *pad_name(Binding binding) {
    const int input = pad_input_of(binding);
    if (input < 0) return nullptr;
    for (const PadName &p : kPadNames)
        if (static_cast<int>(p.input) == input) return &p;
    return nullptr;
}

// Splits `text` at `separator`, which must not start or end a part: a lone
// "/" is the slash key, and "Keypad Plus" is spelled out.
std::vector<std::string_view> split(std::string_view text, std::string_view separator) {
    std::vector<std::string_view> parts;
    while (!text.empty()) {
        const std::size_t at = text.find(separator);
        parts.push_back(at == std::string_view::npos ? text : text.substr(0, at));
        text = at == std::string_view::npos ? std::string_view{} : text.substr(at + separator.size());
    }
    return parts;
}

bool parse_chord(std::string_view text, Chord &out) {
    const std::vector<std::string_view> parts = split(trim(text), kJoin);
    if (parts.empty() || parts.size() > kChordInputs) return false;
    Chord parsed{};
    for (std::size_t i = 0; i < parts.size(); ++i) {
        parsed.inputs[i] = from_name(parts[i]);
        if (parsed.inputs[i] == kNone) return false;
    }
    if (!valid(parsed)) return false;
    out = parsed;
    return true;
}

// The effect bits of the port's own features, whatever group the menu shows
// them in: every action that is neither a PSP button nor a stick.
std::uint64_t port_bits() {
    static const std::uint64_t bits = [] {
        std::uint64_t mask = 1ull << 63; // the port's reserved chords (chords.cpp)
        for (std::size_t i = 0; i < kActions; ++i)
            if (kButtonBits[i] == 0u &&
                !(i >= static_cast<std::size_t>(Action::StickUp) &&
                    i <= static_cast<std::size_t>(Action::StickRight)) &&
                !(i >= static_cast<std::size_t>(Action::CameraUp) &&
                    i <= static_cast<std::size_t>(Action::CameraRight)))
                mask |= 1ull << (32u + i);
        return mask;
    }();
    return bits;
}

// Actions the game reads held while other buttons are pressed: moving, L
// (the item bar) and R (guarding, aiming).
bool read_held(std::size_t target) {
    if (target >= kActions) return false;
    switch (static_cast<Action>(target)) {
    case Action::StickUp:
    case Action::StickLeft:
    case Action::StickDown:
    case Action::StickRight:
    case Action::L:
    case Action::R:
        return true;
    default:
        return false;
    }
}

} // namespace

bool valid(const Chord &c) {
    const std::size_t n = c.size();
    if (n == 0u) return false;
    for (std::size_t i = n; i < kChordInputs; ++i)
        if (c.inputs[i] != kNone) return false;
    for (std::size_t i = 0; i < n; ++i) {
        if (key_position(c.inputs[i]) < 0 && mouse_button_of(c.inputs[i]) == 0 && !is_pad(c.inputs[i])) return false;
        if (!same_device(c.inputs[i], c.inputs[0])) return false;
        for (std::size_t j = i + 1u; j < n; ++j)
            if (c.inputs[i] == c.inputs[j]) return false;
    }
    return true;
}

const ActionInfo &info(Action action) {
    return kInfo[static_cast<std::size_t>(action)];
}

ActionGroup group_of(Action action) {
    switch (action) {
    case Action::StickUp:
    case Action::StickLeft:
    case Action::StickDown:
    case Action::StickRight:
    case Action::Cross:
        return ActionGroup::Movement;
    case Action::Triangle:
    case Action::Circle:
    case Action::TriangleCircle:
    case Action::R:
        return ActionGroup::Attacks;
    case Action::Square:
    case Action::L:
    case Action::ItemLeft:
    case Action::ItemRight:
        return ActionGroup::Items;
    case Action::CameraUp:
    case Action::CameraLeft:
    case Action::CameraDown:
    case Action::CameraRight:
    case Action::Up:
    case Action::Left:
    case Action::Down:
    case Action::Right:
    case Action::LockOn:
        return ActionGroup::Camera;
    case Action::Start:
    case Action::Select:
        return ActionGroup::System;
    default:
        return ActionGroup::Port;
    }
}

const char *group_name(ActionGroup group) {
    switch (group) {
    case ActionGroup::Movement:
        return "Movement";
    case ActionGroup::Attacks:
        return "Attacks";
    case ActionGroup::Items:
        return "Items";
    case ActionGroup::Camera:
        return "Camera";
    case ActionGroup::System:
        return "System";
    default:
        return "Port features";
    }
}

Context context_of(Action action) {
    switch (action) {
    case Action::FrameStep:
        return Context::PhotoMode;
    case Action::Screenshot:
        return Context::Anywhere;
    default:
        return Context::Game;
    }
}

std::uint32_t buttons_of(Action action) {
    return kButtonBits[static_cast<std::size_t>(action)];
}

std::string buttons_label(std::uint32_t buttons) {
    std::string text;
    for (const ButtonName &b : kButtonNames)
        if ((buttons & b.bit) != 0u) text += (text.empty() ? "" : kJoin) + std::string(b.symbol);
    return text;
}

std::string format_buttons(std::uint32_t buttons) {
    std::string text;
    for (const ButtonName &b : kButtonNames)
        if ((buttons & b.bit) != 0u) text += (text.empty() ? "" : kJoin) + std::string(b.name);
    return text;
}

bool parse_buttons(std::string_view text, std::uint32_t &buttons) {
    std::uint32_t parsed = 0u;
    for (const std::string_view part : split(trim(text), kJoin)) {
        bool known = false;
        for (const ButtonName &b : kButtonNames)
            if (equal_ignoring_case(trim(part), b.name)) {
                parsed |= b.bit;
                known = true;
            }
        if (!known) return false;
    }
    if (parsed == 0u) return false;
    buttons = parsed;
    return true;
}

bool acts_on_release(Action action) {
    return action == Action::LockOn;
}

std::string name(Binding binding) {
    if (const int button = mouse_button_of(binding)) return kMouseNames[button - 1];
    if (const PadName *p = pad_name(binding)) return std::string(kPadPrefix) + p->name;
    if (is_pad(binding)) return std::string(kPadPrefix) + std::to_string(pad_input_of(binding));
    const int position = key_position(binding);
    if (position < 0) return {};
    for (const KeyName &k : kKeyNames)
        if (k.position == position) return k.name;
    return "Key " + std::to_string(position);
}

Binding from_name(std::string_view text) {
    text = trim(text);
    for (std::size_t i = 0; i < std::size(kMouseNames); ++i)
        if (equal_ignoring_case(text, kMouseNames[i])) return mouse_button(static_cast<int>(i) + 1);
    if (text.size() > kPadPrefix.size() && equal_ignoring_case(text.substr(0, kPadPrefix.size()), kPadPrefix)) {
        const std::string_view rest = text.substr(kPadPrefix.size());
        for (const PadName &p : kPadNames)
            if (equal_ignoring_case(rest, p.name)) return pad(p.input);
        return kNone;
    }
    for (const KeyName &k : kKeyNames)
        if (equal_ignoring_case(text, k.name)) return key(k.position);
    // SDL's own names for the two keys named differently here.
    if (equal_ignoring_case(text, "Return")) return key(40);
    if (equal_ignoring_case(text, "Escape")) return key(41);
    if (text.size() > 4u && equal_ignoring_case(text.substr(0, 4), "Key ")) {
        const std::string number(text.substr(4));
        char *end = nullptr;
        const unsigned long value = std::strtoul(number.c_str(), &end, 10);
        if (end != number.c_str() && *end == '\0' && value > 0u && value < kKeyPositions)
            return key(static_cast<std::uint16_t>(value));
    }
    return kNone;
}

std::string label(Binding binding, PadStyle style) {
    const PadName *p = pad_name(binding);
    if (p == nullptr) return name(binding);
    switch (style) {
    case PadStyle::Xbox:
        return p->xbox;
    case PadStyle::PlayStation:
        return p->playstation;
    case PadStyle::Nintendo:
        return p->nintendo;
    case PadStyle::Generic:
        break;
    }
    return p->name;
}

std::string label(const Chord &c, PadStyle style) {
    std::string text;
    for (const Binding b : c.held()) text += (text.empty() ? "" : kJoin) + label(b, style);
    return text;
}

std::string format(const Chord &c) {
    std::string text;
    for (const Binding b : c.held()) text += (text.empty() ? "" : kJoin) + name(b);
    return text;
}

std::string format(const Slots &slots) {
    std::string text;
    for (const Chord &c : slots) {
        if (c.empty()) continue;
        if (!text.empty()) text += kSeparator;
        text += format(c);
    }
    return text;
}

bool parse(std::string_view text, Slots &slots) {
    Slots parsed{};
    std::size_t count = 0;
    for (const std::string_view part : split(trim(text), kSeparator)) {
        if (count == kSlots || !parse_chord(part, parsed[count])) return false;
        ++count;
    }
    slots = parsed;
    return true;
}

std::size_t count(const Slots &slots) {
    return static_cast<std::size_t>(
        std::count_if(slots.begin(), slots.end(), [](const Chord &c) { return !c.empty(); }));
}

bool add(Slots &slots, const Chord &c) {
    if (!valid(c) || std::find(slots.begin(), slots.end(), c) != slots.end()) return false;
    for (Chord &slot : slots) {
        if (!slot.empty()) continue;
        slot = c;
        return true;
    }
    return false;
}

bool replace(Slots &slots, std::size_t slot, const Chord &c) {
    if (slot >= kSlots || slots[slot].empty() || !valid(c)) return false;
    if (slots[slot] == c) {
        slots[slot] = c; // the same inputs, perhaps in another order
        return true;
    }
    const auto same = std::find(slots.begin(), slots.end(), c);
    slots[slot] = c;
    if (same != slots.end()) clear(slots, static_cast<std::size_t>(same - slots.begin()));
    return true;
}

bool clear(Slots &slots, std::size_t slot) {
    if (slot >= kSlots || slots[slot].empty()) return false;
    std::move(slots.begin() + static_cast<std::ptrdiff_t>(slot) + 1, slots.end(),
        slots.begin() + static_cast<std::ptrdiff_t>(slot));
    slots.back() = Chord{};
    // Earlier files may have left a gap; keep the chords packed.
    const auto end = std::stable_partition(slots.begin(), slots.end(), [](const Chord &c) { return !c.empty(); });
    std::fill(end, slots.end(), Chord{});
    return true;
}

bool remove(Slots &slots, const Chord &c) {
    const auto found = std::find(slots.begin(), slots.end(), c);
    if (c.empty() || found == slots.end()) return false;
    return clear(slots, static_cast<std::size_t>(found - slots.begin()));
}

bool add(Bindings &bindings, Action action, const Chord &c) {
    return add(bindings[static_cast<std::size_t>(action)], c);
}
bool replace(Bindings &bindings, Action action, std::size_t slot, const Chord &c) {
    return replace(bindings[static_cast<std::size_t>(action)], slot, c);
}
bool clear(Bindings &bindings, Action action, std::size_t slot) {
    return clear(bindings[static_cast<std::size_t>(action)], slot);
}
bool remove(Bindings &bindings, Action action, const Chord &c) {
    return remove(bindings[static_cast<std::size_t>(action)], c);
}

const Slots &Table::slots(std::size_t target) const {
    if (target < kActions) return actions[target];
    const Combo &c = combos[target - kActions];
    return pad ? c.pad : c.keys;
}

Context Table::context(std::size_t target) const {
    return target < kActions ? context_of(static_cast<Action>(target)) : Context::Game;
}

std::uint64_t Table::effect(std::size_t target) const {
    if (target >= kActions) return combos[target - kActions].buttons & kComboButtons;
    return kButtonBits[target] != 0u ? kButtonBits[target] : 1ull << (32u + target);
}

bool port_effect(std::uint64_t effect) {
    return (effect & port_bits()) != 0u;
}

bool waits_for(std::uint64_t shorter, std::uint64_t longer) {
    // L, R and moving held first are what a player does anyway: the longer
    // chord only adds to them.
    constexpr std::uint64_t kHeldFirst = 0x0300u | (0xFull << 32u);
    if ((shorter & ~longer) == 0u && (shorter & ~kHeldFirst) == 0u) return false;
    const bool longer_reaches_game = (longer & ~port_bits()) != 0u;
    return port_effect(shorter) || longer_reaches_game;
}

std::vector<Conflict> conflicts(const Table &table, std::size_t target) {
    const auto taps = [](std::size_t t) { return t < kActions && acts_on_release(static_cast<Action>(t)); };
    std::vector<Conflict> found;
    const Context mine_when = table.context(target);
    for (const Chord &c : table.slots(target)) {
        if (c.empty()) continue;
        for (std::size_t other = 0; other < table.size(); ++other) {
            if (other == target) continue;
            const Context their_when = table.context(other);
            if (mine_when != their_when && mine_when != Context::Anywhere && their_when != Context::Anywhere) continue;
            for (const Chord &theirs : table.slots(other)) {
                if (theirs.empty()) continue;
                if (theirs == c) {
                    found.push_back({Conflict::Kind::Same, other, c, theirs});
                } else if (theirs.part_of(c)) {
                    // Theirs is part of mine: it waits for mine, or is let go
                    // while mine is held. A tap acts only when its chord was
                    // pressed alone, so holding it as part of mine never sets
                    // it off.
                    if (taps(other)) continue;
                    if (read_held(other) && (table.effect(other) & ~table.effect(target)) != 0u)
                        found.push_back({Conflict::Kind::Held, other, c, theirs});
                    else if (waits_for(table.effect(other), table.effect(target)))
                        found.push_back({Conflict::Kind::Part, other, c, theirs});
                } else if (c.part_of(theirs)) {
                    if (taps(target)) continue; // as above, the other way round
                    if (read_held(target) && (table.effect(target) & ~table.effect(other)) != 0u)
                        continue; // theirs reports it as Held
                    if (waits_for(table.effect(target), table.effect(other)))
                        found.push_back({Conflict::Kind::Contains, other, c, theirs});
                }
            }
        }
    }
    return found;
}

std::vector<Conflict> conflicts(const Bindings &bindings, Action action) {
    return conflicts(Table{bindings, {}, false}, static_cast<std::size_t>(action));
}

MouseTurn mouse_turn(
    float counts_x, float counts_y, float degrees_per_count, bool invert_x, bool invert_y, float scale) {
    const float factor = degrees_per_count * scale;
    return {counts_x * factor * (invert_x ? -1.0f : 1.0f), counts_y * factor * (invert_y ? -1.0f : 1.0f)};
}

} // namespace mhp3rd::input
