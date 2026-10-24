#include "input/presets.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace mhp2g::input {
namespace {

// Key positions by USB HID usage, the values SDL's scancodes have.
namespace hid {
constexpr std::uint16_t A = 4, C = 6, D = 7, E = 8, F = 9, H = 11, I = 12, J = 13, K = 14, L = 15, O = 18;
constexpr std::uint16_t Q = 20, S = 22, T = 23, U = 24, W = 26, X = 27, Z = 29;
constexpr std::uint16_t Return = 40, Backspace = 42, Tab = 43, Space = 44, Grave = 53, Period = 55;
constexpr std::uint16_t F7 = 64, F12 = 69, PrintScreen = 70;
constexpr std::uint16_t Right = 79, Left = 80, Down = 81, Up = 82;
constexpr std::uint16_t Keypad4 = 92, Keypad5 = 93, Keypad6 = 94, Keypad8 = 96;
constexpr std::uint16_t LeftShift = 225, RightShift = 229;
} // namespace hid

class Builder {
public:
    Builder &set(Action action, Chord first, Chord second = {}, Chord third = {}, Chord fourth = {}) {
        bindings_[static_cast<std::size_t>(action)] = {first, second, third, fourth};
        return *this;
    }
    Builder &set(Action action, Binding first, Binding second = kNone) {
        return set(action, single(first), second != kNone ? single(second) : Chord{});
    }
    // What every keyboard preset has: the port's own binds, on keys no
    // preset gives the game.
    Builder &host_keys() {
        // The key under Esc, which nothing else is bound to.
        set(Action::FastForward, key(hid::Grave));
        // F12 as in many PC games, and Print Screen where the system leaves
        // it to the game.
        set(Action::Screenshot, key(hid::F12), key(hid::PrintScreen));
        // As in video players: . steps one frame.
        set(Action::FrameStep, key(hid::Period));
        // Beside F6, the free camera's key: nothing else uses it.
        set(Action::HideHud, key(hid::F7));
        // T for target, which no preset gives the game.
        return set(Action::LockOn, key(hid::T));
    }
    // With the mouse in hand, its middle button locks on as well, as in many
    // PC action games; the presets that use the mouse leave it free.
    Builder &mouse_lock_on() { return set(Action::LockOn, single(mouse_button(2)), single(key(hid::T))); }
    // The pad's: a screenshot on a chord of the camera stick's button, which
    // the game does not use, and the D-pad button the free camera leaves
    // free for frame step. `mirrored` puts both on the other side, as the
    // left-handed layout does.
    //
    // Lock-on goes on the camera stick's button itself. The game never reads
    // it, and a lock-on acts only on a press of that button alone (see
    // acts_on_release), so the screenshot chord, L3 + R3 for the menu and
    // Back + R3 for the free camera, which all hold it, never lock on.
    Builder &host_pad(bool mirrored = false) {
        set(Action::LockOn, pad(mirrored ? PadInput::LeftStick : PadInput::RightStick));
        if (mirrored)
            set(Action::Screenshot, chord(pad(PadInput::LeftStick), pad(PadInput::West)));
        else
            set(Action::Screenshot, chord(pad(PadInput::RightStick), pad(PadInput::DpadLeft)));
        return set(Action::FrameStep, pad(mirrored ? PadInput::East : PadInput::DpadRight));
    }
    // The item bar (#198): L + □ and L + ○ on inputs nothing else in the
    // layout uses, so no other input waits for them: the mouse's side
    // buttons, or a pad's upper back paddles where it has them.
    Builder &items(bool mouse) {
        set(Action::ItemLeft, mouse ? mouse_button(4) : pad(PadInput::LeftPaddle1));
        return set(Action::ItemRight, mouse ? mouse_button(5) : pad(PadInput::RightPaddle1));
    }
    [[nodiscard]] const Bindings &done() const { return bindings_; }

private:
    Bindings bindings_{};
};

// W A S D to move with the mouse in the other hand.
Bindings default_keys() {
    Builder b;
    b.set(Action::StickUp, key(hid::W))
        .set(Action::StickLeft, key(hid::A))
        .set(Action::StickDown, key(hid::S))
        .set(Action::StickRight, key(hid::D))
        // The game's attacks on the mouse, as on most PC action games; ○
        // also talks and confirms, so it has a key too.
        .set(Action::Triangle, mouse_button(1))
        .set(Action::Circle, mouse_button(3), key(hid::F))
        .set(Action::Cross, key(hid::Space))
        .set(Action::Square, key(hid::E))
        // L puts the camera behind the hunter, R guards, runs and aims.
        .set(Action::L, key(hid::Q))
        .set(Action::R, key(hid::LeftShift))
        .set(Action::Start, key(hid::Return), key(hid::Tab))
        .set(Action::Select, key(hid::Backspace))
        .set(Action::Up, key(hid::Up))
        .set(Action::Left, key(hid::Left))
        .set(Action::Down, key(hid::Down))
        .set(Action::Right, key(hid::Right))
        // The camera without a mouse, where the old layout moved the hunter.
        .set(Action::CameraUp, key(hid::I))
        .set(Action::CameraLeft, key(hid::J))
        .set(Action::CameraDown, key(hid::K))
        .set(Action::CameraRight, key(hid::L))
        .items(true)
        .host_keys()
        .mouse_lock_on();
    return b.done();
}

// The keyboard-only layout of earlier versions: I J K L to move and the face
// buttons on Z X A S.
Bindings classic_keys() {
    Builder b;
    b.set(Action::StickUp, key(hid::I))
        .set(Action::StickLeft, key(hid::J))
        .set(Action::StickDown, key(hid::K))
        .set(Action::StickRight, key(hid::L))
        .set(Action::Triangle, key(hid::S))
        .set(Action::Circle, key(hid::X))
        .set(Action::Cross, key(hid::Z))
        .set(Action::Square, key(hid::A))
        .set(Action::L, key(hid::Q))
        .set(Action::R, key(hid::W))
        .set(Action::Start, key(hid::Return))
        .set(Action::Select, key(hid::RightShift), key(hid::Backspace))
        .set(Action::Up, key(hid::Up))
        .set(Action::Left, key(hid::Left))
        .set(Action::Down, key(hid::Down))
        .set(Action::Right, key(hid::Right))
        .items(true)
        .host_keys();
    return b.done();
}

// The mouse in the left hand and I J K L under the right, with the keys
// around them mirrored from the default: U for E, O for Q, H for F.
Bindings left_handed_keys() {
    Builder b;
    b.set(Action::StickUp, key(hid::I))
        .set(Action::StickLeft, key(hid::J))
        .set(Action::StickDown, key(hid::K))
        .set(Action::StickRight, key(hid::L))
        .set(Action::Triangle, mouse_button(1))
        .set(Action::Circle, mouse_button(3), key(hid::H))
        .set(Action::Cross, key(hid::Space))
        .set(Action::Square, key(hid::U))
        .set(Action::L, key(hid::O))
        .set(Action::R, key(hid::RightShift))
        .set(Action::Start, key(hid::Return))
        .set(Action::Select, key(hid::Backspace))
        .set(Action::Up, key(hid::Up))
        .set(Action::Left, key(hid::Left))
        .set(Action::Down, key(hid::Down))
        .set(Action::Right, key(hid::Right))
        .set(Action::CameraUp, key(hid::Keypad8))
        .set(Action::CameraLeft, key(hid::Keypad4))
        .set(Action::CameraDown, key(hid::Keypad5))
        .set(Action::CameraRight, key(hid::Keypad6))
        .items(true)
        .host_keys()
        .mouse_lock_on();
    return b.done();
}

// The PSP's layout on a gamepad: the face buttons where the PSP has them,
// the shoulders and the triggers both L and R.
Bindings default_pad() {
    Builder b;
    b.set(Action::Triangle, pad(PadInput::North))
        .set(Action::Circle, pad(PadInput::East))
        .set(Action::Cross, pad(PadInput::South))
        .set(Action::Square, pad(PadInput::West))
        .set(Action::L, pad(PadInput::LeftShoulder), pad(PadInput::LeftTrigger))
        .set(Action::R, pad(PadInput::RightShoulder), pad(PadInput::RightTrigger))
        .set(Action::Start, pad(PadInput::Start))
        .set(Action::Select, pad(PadInput::Back))
        .set(Action::Up, pad(PadInput::DpadUp))
        .set(Action::Left, pad(PadInput::DpadLeft))
        .set(Action::Down, pad(PadInput::DpadDown))
        .set(Action::Right, pad(PadInput::DpadRight))
        .items(false)
        .host_pad();
    return b.done();
}

// Recent action games: the right trigger attacks (and shoots a bow), the
// right shoulder does the second attack (and fires a bowgun), the left
// trigger guards and aims. The face buttons keep working.
Bindings modern_pad() {
    Builder b;
    b.set(Action::Triangle, pad(PadInput::North), pad(PadInput::RightTrigger))
        .set(Action::Circle, pad(PadInput::East), pad(PadInput::RightShoulder))
        .set(Action::Cross, pad(PadInput::South))
        .set(Action::Square, pad(PadInput::West))
        .set(Action::L, pad(PadInput::LeftShoulder))
        .set(Action::R, pad(PadInput::LeftTrigger))
        .set(Action::Start, pad(PadInput::Start))
        .set(Action::Select, pad(PadInput::Back))
        .set(Action::Up, pad(PadInput::DpadUp))
        .set(Action::Left, pad(PadInput::DpadLeft))
        .set(Action::Down, pad(PadInput::DpadDown))
        .set(Action::Right, pad(PadInput::DpadRight))
        .items(false)
        .host_pad();
    return b.done();
}

Bindings modern_keys() {
    Bindings keys = default_keys();
    keys[static_cast<std::size_t>(Action::TriangleCircle)] = {single(mouse_button(4)), single(key(hid::C))};
    // The side button that scrolls the items left does △ + ○ here, so the
    // other one is left free too.
    keys[static_cast<std::size_t>(Action::ItemLeft)] = {};
    keys[static_cast<std::size_t>(Action::ItemRight)] = {};
    return keys;
}

// The pad mirrored: the right stick moves, the D-pad is the face buttons and
// the face buttons the D-pad, and the shoulders change sides.
Bindings left_handed_pad() {
    Builder b;
    b.set(Action::Triangle, pad(PadInput::DpadUp))
        .set(Action::Circle, pad(PadInput::DpadRight))
        .set(Action::Cross, pad(PadInput::DpadDown))
        .set(Action::Square, pad(PadInput::DpadLeft))
        .set(Action::L, pad(PadInput::RightShoulder), pad(PadInput::RightTrigger))
        .set(Action::R, pad(PadInput::LeftShoulder), pad(PadInput::LeftTrigger))
        .set(Action::Start, pad(PadInput::Start))
        .set(Action::Select, pad(PadInput::Back))
        .set(Action::Up, pad(PadInput::North))
        .set(Action::Left, pad(PadInput::West))
        .set(Action::Down, pad(PadInput::South))
        .set(Action::Right, pad(PadInput::East))
        .items(false)
        .host_pad(true);
    return b.done();
}

std::array<Layout, kPresets> make_layouts() {
    std::array<Layout, kPresets> layouts{};
    layouts[static_cast<std::size_t>(Preset::Default)] = {default_keys(), default_pad(), false};
    layouts[static_cast<std::size_t>(Preset::Modern)] = {modern_keys(), modern_pad(), false};
    layouts[static_cast<std::size_t>(Preset::LeftHanded)] = {left_handed_keys(), left_handed_pad(), true};
    layouts[static_cast<std::size_t>(Preset::Classic)] = {classic_keys(), default_pad(), false};
    return layouts;
}

constexpr PresetInfo kInfo[kPresets] = {
    {"default", "Default",
        "W A S D and the mouse; the PSP's buttons where a gamepad has them, with the triggers as L and R."},
    {"modern", "Modern",
        "Like recent action games: on a gamepad RT attacks and shoots a bow, RB does the second attack and fires a "
        "bowgun, LT guards and aims. On the keyboard, the side mouse button or C does △ + ○."},
    {"left_handed", "Left-handed",
        "Mirrored: the mouse in the left hand and I J K L to move; on a gamepad the right stick moves, the D-pad "
        "does the face buttons and the shoulders change sides."},
    {"classic", "Classic keyboard",
        "The keys of earlier versions, for play without a mouse: I J K L move, Z X A S are the face buttons, Q and W "
        "are L and R. The gamepad as in Default."},
};

constexpr std::string_view kUserPrefix = "user:";

bool equal_ignoring_case(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

} // namespace

Slots &slots(Layout &layout, bool pad, std::size_t target) {
    if (target < kActions) return (pad ? layout.pad : layout.keys)[target];
    Combo &c = layout.combos[target - kActions];
    return pad ? c.pad : c.keys;
}

const Slots &slots(const Layout &layout, bool pad, std::size_t target) {
    return slots(const_cast<Layout &>(layout), pad, target);
}

const PresetInfo &info(Preset preset) {
    return kInfo[static_cast<std::size_t>(preset)];
}

const Layout &layout(Preset preset) {
    static const std::array<Layout, kPresets> layouts = make_layouts();
    return layouts[static_cast<std::size_t>(preset)];
}

std::optional<Preset> preset_from_id(std::string_view id) {
    for (std::size_t i = 0; i < kPresets; ++i)
        if (id == kInfo[i].id) return static_cast<Preset>(i);
    return std::nullopt;
}

std::optional<Preset> matching_preset(const Layout &candidate) {
    for (std::size_t i = 0; i < kPresets; ++i)
        if (layout(static_cast<Preset>(i)) == candidate) return static_cast<Preset>(i);
    return std::nullopt;
}

std::string clean_preset_name(std::string_view name) {
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.remove_prefix(1);
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.remove_suffix(1);
    std::string cleaned;
    for (const char c : name) {
        // One line in settings.ini, and no character the file gives a meaning.
        if (c == '\n' || c == '\r' || c == '=' || c == '#') continue;
        cleaned += c;
    }
    // Cut at a whole UTF-8 character.
    if (cleaned.size() > kMaxPresetName) {
        std::size_t cut = kMaxPresetName;
        while (cut > 0u && (static_cast<unsigned char>(cleaned[cut]) & 0xC0u) == 0x80u) --cut;
        cleaned.resize(cut);
    }
    while (!cleaned.empty() && cleaned.back() == ' ') cleaned.pop_back();
    return cleaned;
}

std::string unique_preset_name(const std::vector<UserPreset> &presets, std::string_view base) {
    const auto taken = [&](std::string_view name) {
        for (const UserPreset &p : presets)
            if (equal_ignoring_case(p.name, name)) return true;
        for (const PresetInfo &shipped : kInfo)
            if (equal_ignoring_case(shipped.name, name)) return true;
        return false;
    };
    const std::string cleaned = clean_preset_name(base);
    const std::string start = cleaned.empty() ? std::string("Custom") : cleaned;
    if (!taken(start)) return start;
    for (int n = 2;; ++n) {
        const std::string suffix = " " + std::to_string(n);
        std::string candidate =
            clean_preset_name(start.substr(0, kMaxPresetName - std::min(kMaxPresetName, suffix.size())));
        candidate += suffix;
        if (!taken(candidate)) return candidate;
    }
}

std::string format(const PresetChoice &choice) {
    if (choice.shipped) return info(*choice.shipped).id;
    return std::string(kUserPrefix) + choice.user;
}

std::optional<PresetChoice> parse_choice(std::string_view text) {
    if (const std::optional<Preset> shipped = preset_from_id(text)) return PresetChoice{shipped, {}};
    if (text.size() > kUserPrefix.size() && text.substr(0, kUserPrefix.size()) == kUserPrefix)
        return PresetChoice{std::nullopt, std::string(text.substr(kUserPrefix.size()))};
    return std::nullopt;
}

Layout layout_from_earlier(const Bindings &keys, std::string_view trigger_profile) {
    Layout result = layout(Preset::Default);
    result.keys = keys;
    const bool bows = trigger_profile == "bows";
    const bool bowguns = trigger_profile == "bowguns";
    if (!bows && !bowguns) return result;
    // R moved onto LT to aim, and the attack onto RT; RB, △ and ○ kept theirs.
    const auto slots = [&](Action action) -> Slots & { return result.pad[static_cast<std::size_t>(action)]; };
    slots(Action::L) = {single(pad(PadInput::LeftShoulder)), {}};
    slots(Action::R) = {single(pad(PadInput::RightShoulder)), single(pad(PadInput::LeftTrigger))};
    if (bows)
        slots(Action::Triangle) = {single(pad(PadInput::North)), single(pad(PadInput::RightTrigger))};
    else
        slots(Action::Circle) = {single(pad(PadInput::East)), single(pad(PadInput::RightTrigger))};
    return result;
}

} // namespace mhp2g::input
