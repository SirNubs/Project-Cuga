#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Bindings for the game (#94, #165, #198): which keys, mouse buttons and
// gamepad buttons press each PSP control, alone or held together.
//
// An input is a key, by its position on the keyboard (a USB HID usage, the
// numbers SDL's scancodes use), a mouse button, or a gamepad button or
// trigger, by its place on the pad. Positions rather than characters or
// labels, so W A S D stay where they are on any keyboard layout and the pad's
// bottom button is the bottom button on any pad. A chord is one input, or up
// to four held together on one device, in any order: the keyboard and the
// mouse are one device, a gamepad another. Nothing here needs SDL, so
// settings.ini is read the same way in every build.
namespace mhp2g::input {

enum class Action : std::uint8_t {
    StickUp,
    StickLeft,
    StickDown,
    StickRight,
    Triangle,
    Circle,
    Cross,
    Square,
    L,
    R,
    Start,
    Select,
    Up,
    Left,
    Down,
    Right,
    // The HD release's second stick, pushed fully that way while held.
    CameraUp,
    CameraLeft,
    CameraDown,
    CameraRight,
    // △ and ○ in the same frame: the game's combined attacks, which two
    // fingers or two keys do not always manage together.
    TriangleCircle,
    // Not a PSP control: runs the game faster than real time while held, or
    // turns that on and off (kernel/fast_forward.hpp). Single player only.
    FastForward,
    // Not PSP controls either (#187): saves the game's picture as a PNG, and
    // in the free camera's photo mode runs the game on by one frame.
    Screenshot,
    FrameStep,
    // Not a PSP control: hides the game's HUD and brings it back
    // (gpu/game_hud.hpp).
    HideHud,
    // L + □ and L + ○ (#198): the item bar one item to the left or right,
    // from one input or from a chord of the player's.
    ItemLeft,
    ItemRight,
    // Not a PSP control: locks the camera onto a large monster and lets it
    // go again (camera/lock_on.hpp).
    LockOn,
    Count
};
inline constexpr std::size_t kActions = static_cast<std::size_t>(Action::Count);
// Every action takes up to four chords on each device. Earlier versions kept
// two, so what they wrote always fits.
inline constexpr std::size_t kSlots = 4u;

// 0: none. 1-511: a key position. kMouse + 1..5: a mouse button, numbered as
// SDL numbers them (left, middle, right, back, forward). kPad + 1 + n: the
// gamepad input n below.
using Binding = std::uint16_t;
inline constexpr Binding kNone = 0u;
inline constexpr Binding kMouse = 0x200u;
inline constexpr Binding kPad = 0x300u;
inline constexpr std::uint16_t kKeyPositions = 512u;

// Gamepad inputs by place. The buttons have the values of SDL3's
// SDL_GamepadButton (the renderer checks that); the triggers, which SDL
// reports as axes, count as held past the trigger point.
enum class PadInput : std::uint8_t {
    South,
    East,
    West,
    North,
    Back,
    Guide,
    Start,
    LeftStick,
    RightStick,
    LeftShoulder,
    RightShoulder,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Misc1,
    RightPaddle1,
    LeftPaddle1,
    RightPaddle2,
    LeftPaddle2,
    Touchpad,
    Misc2,
    Misc3,
    Misc4,
    Misc5,
    Misc6,
    ButtonCount,
    LeftTrigger = 32,
    RightTrigger,
    Count
};

[[nodiscard]] constexpr Binding key(std::uint16_t position) {
    return position;
}
[[nodiscard]] constexpr Binding mouse_button(int button) {
    return static_cast<Binding>(kMouse + button);
}
[[nodiscard]] constexpr Binding pad(PadInput input) {
    return static_cast<Binding>(kPad + 1u + static_cast<unsigned>(input));
}
// The key position, or -1 for anything else.
[[nodiscard]] constexpr int key_position(Binding binding) {
    return binding != kNone && binding < kKeyPositions ? binding : -1;
}
// The mouse button, 1-5, or 0 for anything else.
[[nodiscard]] constexpr int mouse_button_of(Binding binding) {
    return binding > kMouse && binding <= kMouse + 5u ? binding - kMouse : 0;
}
// The gamepad input, or -1 for anything else.
[[nodiscard]] constexpr int pad_input_of(Binding binding) {
    return binding > kPad && binding <= kPad + static_cast<unsigned>(PadInput::Count) ? binding - kPad - 1 : -1;
}
[[nodiscard]] constexpr bool is_pad(Binding binding) {
    return pad_input_of(binding) >= 0;
}

// Up to four inputs held together, in the order they were pressed; a chord
// of one is a single input. Two chords are the same when they hold the same
// inputs, whatever the order.
inline constexpr std::size_t kChordInputs = 4u;
struct Chord {
    std::array<Binding, kChordInputs> inputs{}; // packed at the front

    [[nodiscard]] constexpr std::size_t size() const {
        std::size_t n = 0;
        while (n < kChordInputs && inputs[n] != kNone) ++n;
        return n;
    }
    [[nodiscard]] constexpr bool empty() const { return inputs[0] == kNone; }
    // More than one input.
    [[nodiscard]] constexpr bool combined() const { return inputs[1] != kNone; }
    [[nodiscard]] constexpr bool contains(Binding binding) const {
        if (binding == kNone) return false;
        for (const Binding b : inputs)
            if (b == binding) return true;
        return false;
    }
    [[nodiscard]] constexpr std::span<const Binding> held() const { return {inputs.data(), size()}; }
    // Every input of this chord is in `other`.
    [[nodiscard]] constexpr bool part_of(const Chord &other) const {
        for (const Binding b : held())
            if (!other.contains(b)) return false;
        return true;
    }
    friend constexpr bool operator==(const Chord &a, const Chord &b) { return a.size() == b.size() && a.part_of(b); }
};
[[nodiscard]] constexpr Chord single(Binding binding) {
    return {{binding, kNone, kNone, kNone}};
}
[[nodiscard]] constexpr Chord chord(Binding a, Binding b, Binding c = kNone, Binding d = kNone) {
    return {{a, b, c, d}};
}
// The keyboard and the mouse are one device, gamepads another.
[[nodiscard]] constexpr bool same_device(Binding a, Binding b) {
    return is_pad(a) == is_pad(b);
}
// One to four different inputs, all on one device, packed at the front.
[[nodiscard]] bool valid(const Chord &chord);

using Slots = std::array<Chord, kSlots>;
using Bindings = std::array<Slots, kActions>;

struct ActionInfo {
    const char *key;   // in settings.ini, after "input.bind." or "input.pad."
    const char *label; // in the menu
};
[[nodiscard]] const ActionInfo &info(Action action);

// How the menu groups the actions. Anything that is no PSP control, such as
// fast-forward, is a feature of the port.
enum class ActionGroup : std::uint8_t { Movement, Attacks, Items, Camera, System, Port, Count };
inline constexpr std::size_t kActionGroups = static_cast<std::size_t>(ActionGroup::Count);
[[nodiscard]] ActionGroup group_of(Action action);
[[nodiscard]] const char *group_name(ActionGroup group);
// When an action's bindings are read. The PSP's controls and fast-forward
// reach the game only while it has input; frame step is read only in the
// free camera's photo mode, when the game has none, so it may share an input
// with them; a screenshot is taken whenever the game is on screen.
enum class Context : std::uint8_t { Game, PhotoMode, Anywhere };
[[nodiscard]] Context context_of(Action action);
// The SceCtrlButtons an action presses; 0 for the sticks and the port's own.
[[nodiscard]] std::uint32_t buttons_of(Action action);
// Actions that act when their input is let go, and only if it was pressed
// alone: nothing else on the same device held as it went down, and nothing
// else pressed before it came up (TapDetector). Their chords may therefore be
// part of longer chords, such as R3 for lock-on inside R3 + D-pad left for a
// screenshot, without clashing with them.
[[nodiscard]] bool acts_on_release(Action action);

// The PSP's buttons an action of the player's may press together (#198).
inline constexpr std::uint32_t kComboButtons = 0xF3F9u; // △ ○ × □ L R START SELECT and the D-pad
// "L + □" in the menu, in the PSP's order: △ ○ × □ L R, the D-pad, START,
// SELECT. Empty for none.
[[nodiscard]] std::string buttons_label(std::uint32_t buttons);
// "L + Square" as settings.ini keeps it, and back. parse_buttons is false,
// leaving `buttons` alone, for an unknown name or none.
[[nodiscard]] std::string format_buttons(std::uint32_t buttons);
bool parse_buttons(std::string_view text, std::uint32_t &buttons);

// An action the player made (#198): any set of the PSP's buttons, pressed
// together by its chords on each device.
struct Combo {
    std::uint32_t buttons{}; // SceCtrlButtons, within kComboButtons
    Slots keys{};            // the keyboard and the mouse
    Slots pad{};             // gamepads
    friend bool operator==(const Combo &, const Combo &) = default;
};
inline constexpr std::size_t kMaxCombos = 8u;

// "W", "Left Shift", "Mouse Left", "Pad South"; "Key 123" for a position
// without a name. These are the names settings.ini keeps.
[[nodiscard]] std::string name(Binding binding);
// The reverse of name(), ignoring case. kNone if the name is not known.
[[nodiscard]] Binding from_name(std::string_view text);

// How the menu names a gamepad's inputs: by place, or as the pad in use
// labels them.
enum class PadStyle { Generic, Xbox, PlayStation, Nintendo };
// name() for the menu: a pad input as `style` labels it ("LB", "L1", "L"), a
// key or a mouse button as name() has it.
[[nodiscard]] std::string label(Binding binding, PadStyle style);
[[nodiscard]] std::string label(const Chord &chord, PadStyle style);

// Slots as settings.ini keeps them: chords separated by " / ", the inputs of
// a chord by " + ", empty for none. "Left Shift + F / Mouse Right".
[[nodiscard]] std::string format(const Slots &slots);
[[nodiscard]] std::string format(const Chord &chord);
// False, leaving `slots` alone, if a name is not known or a chord is no
// chord (valid()).
bool parse(std::string_view text, Slots &slots);

// The menu's ways of changing an action's chords. The chords stay packed at
// the front of the slots, in the order they were added. Other actions keep
// what they have: a chord bound twice is shown as a conflict, not taken away.
[[nodiscard]] std::size_t count(const Slots &slots);
// Adds a chord after the ones the slots have. False, changing nothing, when
// they have it already, all are full, or it is no chord.
bool add(Slots &slots, const Chord &chord);
bool add(Bindings &bindings, Action action, const Chord &chord);
// Puts a chord in place of the one in `slot`. If the slots have the chord in
// another slot already, that one goes, so it is never bound twice. False,
// changing nothing, when `slot` is empty or the chord is no chord.
bool replace(Slots &slots, std::size_t slot, const Chord &chord);
bool replace(Bindings &bindings, Action action, std::size_t slot, const Chord &chord);
// Removes the chord in `slot`; the ones after it move up. False when there
// is none.
bool clear(Slots &slots, std::size_t slot);
bool clear(Bindings &bindings, Action action, std::size_t slot);
// Removes `chord`, wherever it is. False when there is none.
bool remove(Slots &slots, const Chord &chord);
bool remove(Bindings &bindings, Action action, const Chord &chord);

// One device's bindings as the chords are matched: the actions, then the
// player's own actions. A target is an index into both: an Action, or
// kActions + the number of a combo.
struct Table {
    const Bindings &actions;
    std::span<const Combo> combos;
    bool pad{}; // which of a combo's slots

    [[nodiscard]] std::size_t size() const { return kActions + combos.size(); }
    [[nodiscard]] const Slots &slots(std::size_t target) const;
    [[nodiscard]] Context context(std::size_t target) const;
    // What a target does, as one mask: the PSP buttons it presses in the low
    // 32 bits, and one bit above them for a stick direction or a feature of
    // the port. A chord does what all the targets bound to it do.
    [[nodiscard]] std::uint64_t effect(std::size_t target) const;
};
// The effect bits of the port's own features, which the game never sees.
[[nodiscard]] bool port_effect(std::uint64_t effect);
// Whether a chord should wait to see if a longer one follows, because
// acting at once could do harm: when it works a feature of the port (a
// screenshot, which cannot be taken back), or when the longer chord reaches
// the game, which reads the order buttons come in (□ before L uses an item;
// △ before ○ is no combined attack). Two cases act at once: L, R or moving
// that the longer chord also presses (LB in LB + X for L + □), and a game
// button in a chord only the port reads (D-pad left in R3 + D-pad left for a
// screenshot).
[[nodiscard]] bool waits_for(std::uint64_t shorter, std::uint64_t longer);

// What clashes with a chord of a target. Targets never read at the same time
// (context_of) do not clash.
struct Conflict {
    enum class Kind : std::uint8_t {
        Same,     // another target has the same chord: one press does both
        Part,     // the other's chord is part of this one: that input waits, then does only this
        Contains, // this chord is part of the other's: held together, they do only the other
        Held,     // part of this chord does something the game reads held (L, R, moving), which
                  // stops while the chord is held
    };
    Kind kind{};
    std::size_t other{}; // a target (Table)
    Chord chord;         // the chord of `target` that clashes
    Chord theirs;        // and the chord of `other` it clashes with
};
[[nodiscard]] std::vector<Conflict> conflicts(const Table &table, std::size_t target);
[[nodiscard]] std::vector<Conflict> conflicts(const Bindings &bindings, Action action);

// What the held inputs press, in PSP terms.
struct PadState {
    std::uint32_t buttons{}; // SceCtrlButtons
    int stick_x{};           // -127..127 from the centre, each axis
    int stick_y{};
    int camera_x{}; // the second stick
    int camera_y{};
    bool fast_forward{}; // the fast-forward bind is held
    bool screenshot{};   // the screenshot bind is held
    bool frame_step{};   // the frame step bind is held
    bool hide_hud{};     // the hide-HUD bind is held
    bool lock_on{};      // the lock-on bind is held
};
// At once, with no memory of what came before (Resolver has it): among the
// held inputs, the longest bound chord wins and its inputs do nothing else;
// what is left does the same again. With LB + B bound to one action and B to
// another, holding both does only the first.
[[nodiscard]] PadState read(const Bindings &bindings, const std::function<bool(Binding)> &held);
[[nodiscard]] PadState read(const Table &table, const std::function<bool(Binding)> &held);

// A tap: an input pressed and let go on its own. `held` is whether the
// input is held now, `others` whether any other input of the same device is.
// True once, on the update it is let go, if nothing else was held while it
// was down.
class TapDetector {
public:
    bool update(bool held, bool others) {
        if (held && !held_)
            spoiled_ = others;
        else if (held)
            spoiled_ = spoiled_ || others;
        const bool tap = !held && held_ && !spoiled_;
        held_ = held;
        return tap;
    }

private:
    bool held_{};
    bool spoiled_{};
};

// The mouse's motion as degrees for the camera: positive yaw turns right,
// positive pitch looks down, as camera_input expects. `counts` are relative
// motion; `scale` is what the current camera makes of it (1, or Aim speed's
// share of Camera speed while a bow or a bowgun aims).
struct MouseTurn {
    float yaw{};
    float pitch{};
};
[[nodiscard]] MouseTurn mouse_turn(
    float counts_x, float counts_y, float degrees_per_count, bool invert_x, bool invert_y, float scale);

} // namespace mhp2g::input
