#pragma once

#include "input/bindings.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Control presets (#165): whole layouts for the keyboard and mouse and for
// gamepads, every action included. A few ship with Yakumo and cannot be
// changed; the player's own are kept in settings.ini by name. Nothing here
// needs SDL.
namespace mhp3rd::input {

struct Layout {
    Bindings keys{}; // the keyboard and the mouse
    Bindings pad{};  // gamepads
    // The right stick moves the hunter and the left one turns the camera.
    bool swap_sticks{};
    // The player's own actions (#198): any PSP buttons together. No shipped
    // preset has one.
    std::vector<Combo> combos;
    friend bool operator==(const Layout &, const Layout &) = default;
};

// A layout's bindings on one device, as chords are matched (bindings.hpp):
// the actions, then the player's combos. The layout must outlive it.
[[nodiscard]] inline Table table(const Layout &layout, bool pad) {
    return Table{pad ? layout.pad : layout.keys, layout.combos, pad};
}
// The slots of a target (an Action, or kActions + a combo's number).
[[nodiscard]] Slots &slots(Layout &layout, bool pad, std::size_t target);
[[nodiscard]] const Slots &slots(const Layout &layout, bool pad, std::size_t target);

enum class Preset : std::uint8_t { Default, Modern, LeftHanded, Classic, Count };
inline constexpr std::size_t kPresets = static_cast<std::size_t>(Preset::Count);

struct PresetInfo {
    const char *id;          // in settings.ini
    const char *name;        // in the menu
    const char *description; // in the menu's footer
};
[[nodiscard]] const PresetInfo &info(Preset preset);
[[nodiscard]] const Layout &layout(Preset preset);
[[nodiscard]] std::optional<Preset> preset_from_id(std::string_view id);
// The shipped preset `layout` is exactly, if any.
[[nodiscard]] std::optional<Preset> matching_preset(const Layout &layout);

// A preset the player made, and the shipped preset it was made from, which
// "reset to default" returns an action to. Presets from before this was kept
// count as made from Default.
struct UserPreset {
    std::string name;
    Layout layout;
    Preset base{Preset::Default};
};
inline constexpr std::size_t kMaxUserPresets = 32u;
inline constexpr std::size_t kMaxPresetName = 24u;
// `base`, or `base 2`, `base 3`, ... whichever no preset has yet, ignoring case.
[[nodiscard]] std::string unique_preset_name(const std::vector<UserPreset> &presets, std::string_view base);
// Trims spaces and cuts `name` to kMaxPresetName; empty if nothing is left.
[[nodiscard]] std::string clean_preset_name(std::string_view name);

// Which preset is chosen, as settings.ini keeps it in input.preset: a shipped
// preset's id, or "user:" and the name of one of the player's.
struct PresetChoice {
    std::optional<Preset> shipped; // empty: the player's preset below
    std::string user;
    friend bool operator==(const PresetChoice &, const PresetChoice &) = default;
};
[[nodiscard]] std::string format(const PresetChoice &choice);
[[nodiscard]] std::optional<PresetChoice> parse_choice(std::string_view text);

// Earlier versions had keyboard bindings and a trigger profile for the pad
// ("standard", "bows" or "bowguns") in place of presets. The layout they
// amount to: the keys as they were, and the pad of the Default preset with
// the triggers the profile gave, LT on R and RT on △ or ○ for the shooting
// profiles.
[[nodiscard]] Layout layout_from_earlier(const Bindings &keys, std::string_view trigger_profile);

} // namespace mhp3rd::input
