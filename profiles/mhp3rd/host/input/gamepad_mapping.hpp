#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Gamepad mappings for controllers SDL does not know (#147), without SDL, so
// the setup's logic is tested like the bindings.
//
// SDL drives a controller as a gamepad (buttons by place: south, east,
// shoulders, sticks) only when it has a mapping for it: a line that says which
// of the device's raw buttons, hats and axes is which, such as
//
//   03000000100800000100000000000000,PS1 Controller,a:b2,b:b1,dpup:h0.1,leftx:a0,...,platform:Windows,
//
// Without one the device is only a joystick, and neither the game nor the
// menu reads it. The Controllers screen asks the player to press each control
// in turn, turns the answers into such a line, and keeps it in
// gamecontrollerdb.txt in the data directory, the file format of SDL and of
// the community's SDL_GameControllerDB.
namespace mhp3rd::input::mapping {

// One input of a joystick as a mapping names it: button 3 is "b3", hat 0
// pushed down "h0.4", axis 2 "a2", the positive half of axis 1 "+a1", and an
// axis read the other way round "a0~".
struct Element {
    enum class Kind : std::uint8_t { None, Button, Hat, Axis };
    enum class Range : std::uint8_t { Full, Positive, Negative };
    Kind kind{Kind::None};
    int index{};
    int hat_mask{}; // Hat: 1 up, 2 right, 4 down, 8 left
    Range range{Range::Full};
    bool inverted{};
    [[nodiscard]] bool empty() const { return kind == Kind::None; }
    // The same button, hat direction or axis, whatever the range.
    [[nodiscard]] bool same_input(const Element &other) const;
    friend bool operator==(const Element &, const Element &) = default;
};
[[nodiscard]] std::string text(const Element &element);
// "Button 3", "Hat 0 down", "Axis 1 +", as the screen shows them.
[[nodiscard]] std::string describe(const Element &element);
// The reverse of text(); an empty element for anything else.
[[nodiscard]] Element parse_element(std::string_view text);

// What a mapping can name, in the order the setup asks for them. The axes
// come last: a stick is asked for once per direction it moves in.
enum class Target : std::uint8_t {
    A, // bottom face button
    B, // right
    X, // left
    Y, // top
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    LeftShoulder,
    RightShoulder,
    LeftTrigger,
    RightTrigger,
    Back,
    Start,
    LeftStick,
    RightStick,
    Guide,
    LeftX,
    LeftY,
    RightX,
    RightY,
    Count
};
inline constexpr std::size_t kTargets = static_cast<std::size_t>(Target::Count);
// The field of a mapping line: "a", "dpup", "leftx".
[[nodiscard]] const char *field(Target target);
// Whether the target is a stick's axis, which only an axis can answer.
[[nodiscard]] bool is_axis(Target target);

using Answers = std::array<Element, kTargets>;
// How many targets have an answer.
[[nodiscard]] std::size_t count(const Answers &answers);

// A device name fit for a mapping line: no commas, no line breaks, trimmed,
// "Controller" when nothing is left.
[[nodiscard]] std::string clean_name(std::string_view name);
// The mapping line: "GUID,Name,a:b2,...,platform:Linux," with the answered
// targets in Target order. `platform` as SDL_GetPlatform() has it; SDL reads a
// file's lines only when they carry one.
[[nodiscard]] std::string build(
    std::string_view guid, std::string_view name, const Answers &answers, std::string_view platform);
// The answers a mapping line gives, for the targets above; unknown fields are
// left out.
[[nodiscard]] Answers answers_of(std::string_view line);

// A mappings file, as text. Lines are kept as they are; a line is a mapping
// when it has a GUID, a comma and more, and is no comment.
[[nodiscard]] std::string_view guid_of(std::string_view line);
[[nodiscard]] std::string_view platform_of(std::string_view line);
// The line for this GUID and platform, if the text has one. A line without a
// platform counts for every platform.
[[nodiscard]] std::optional<std::string> find_line(
    std::string_view text, std::string_view guid, std::string_view platform);
// The text with any line for the GUID and platform of `line` replaced by
// `line`, or with `line` added at the end. A new file starts with a comment
// saying what it is.
[[nodiscard]] std::string with_line(std::string_view text, std::string_view line);
// The text without the lines for the GUID and platform. `removed`: whether
// there was one.
[[nodiscard]] std::string without(
    std::string_view text, std::string_view guid, std::string_view platform, bool *removed = nullptr);
// The lines of a text (SDL_GAMECONTROLLERCONFIG may hold several), without
// blanks and comments.
[[nodiscard]] std::vector<std::string> lines(std::string_view text);

// The setup's reading of the device: what changed since it was at rest.
struct Snapshot {
    std::vector<bool> buttons;
    std::vector<std::uint8_t> hats;
    std::vector<std::int16_t> axes;
};
// How far an axis moves before it counts, and how close to rest it must come
// back: half its travel and a quarter of it.
inline constexpr int kAxisPress = 16384;
inline constexpr int kAxisRelease = 8192;
// The input that `now` shows pressed and `rest` did not, or nothing.
// Buttons win over hats and hats over axes, so a pad that reports its D-pad
// both ways is mapped by its hat. For a stick (`axis_only`), only an axis
// resting near its centre counts, moved either way; the direction the setup
// asked for is positive, so moving the other way gives an inverted axis. For
// a button, an axis that rests at its centre gives the half it moved to, and
// one that rests at an end (a trigger) the whole axis, inverted if it rests
// at the top.
[[nodiscard]] std::optional<Element> detect(const Snapshot &rest, const Snapshot &now, bool axis_only);
// Whether everything is back as at rest: no button held that was not, the
// hats back, every axis within kAxisRelease of where it rested.
[[nodiscard]] bool at_rest(const Snapshot &rest, const Snapshot &now);

} // namespace mhp3rd::input::mapping
