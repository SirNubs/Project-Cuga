#include "settings/settings.hpp"

#include "install/user_data.hpp"
#include "platform/utf8_path.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <string_view>
#include <string>
#include <vector>

namespace mhp3rd::settings {
namespace {

// One setting: its key in settings.ini, the variable that overrides it, and
// how both spell its value.
struct Field {
    const char *key;
    const char *variable; // null: no environment override
    std::function<bool(Settings &, const std::string &)> parse;
    std::function<std::string(const Settings &)> format;
    // Reads the variable's value, which is spelled the way the variable has
    // always been. Null: the variable uses the file's spelling.
    std::function<void(Settings &, const char *)> parse_variable;
};

bool parse_bool(const std::string &text, bool &out) {
    if (text == "1" || text == "true" || text == "on" || text == "yes")
        out = true;
    else if (text == "0" || text == "false" || text == "off" || text == "no")
        out = false;
    else
        return false;
    return true;
}

bool parse_float(const std::string &text, float minimum, float maximum, float &out) {
    char *end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0') return false;
    out = std::clamp(value, minimum, maximum);
    return true;
}

bool parse_uint(const std::string &text, std::uint32_t minimum, std::uint32_t maximum, std::uint32_t &out) {
    char *end = nullptr;
    const unsigned long value = std::strtoul(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0') return false;
    out = static_cast<std::uint32_t>(std::clamp<unsigned long>(value, minimum, maximum));
    return true;
}

// A resolution: a multiple of 480x272, or "auto" (0) for the window's own.
bool parse_scale(const std::string &text, std::uint32_t &out) {
    if (text == "auto") {
        out = 0u;
        return true;
    }
    return parse_uint(text, 0u, kMaxInternalScale, out);
}

std::string format_float(float value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.2f", static_cast<double>(value));
    return text;
}

// Flags the host has always read as "set means on", whatever the value.
bool variable_present(const char *) {
    return true;
}

// Flags read the way the pad code reads them: 0, no, off and false are off.
bool variable_flag(const char *text) {
    for (const char *off : {"0", "no", "off", "false"})
        if (std::strcmp(text, off) == 0) return false;
    return true;
}

float variable_float(const char *text, float fallback, float minimum, float maximum) {
    char *end = nullptr;
    const float value = std::strtof(text, &end);
    return std::clamp(end != text ? value : fallback, minimum, maximum);
}

template <typename Enum> struct Names {
    std::vector<std::pair<Enum, const char *>> values;

    bool parse(const std::string &text, Enum &out) const {
        for (const auto &[value, name] : values) {
            if (text == name) {
                out = value;
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] std::string format(Enum value) const {
        for (const auto &[candidate, name] : values)
            if (candidate == value) return name;
        return values.front().second;
    }
};

const Names<PresentMode> kPresentModes{
    {{PresentMode::Fifo, "vsync"}, {PresentMode::Mailbox, "mailbox"}, {PresentMode::Immediate, "immediate"}}};
const Names<Aspect> kAspects{{{Aspect::Original, "original"}, {Aspect::Stretch, "stretch"}, {Aspect::Fill, "fill"}}};
const Names<PerfDisplay> kPerfDisplays{{{PerfDisplay::Off, "off"}, {PerfDisplay::Overlay, "overlay"},
    {PerfDisplay::OverlayAndLog, "overlay+log"}, {PerfDisplay::Log, "log"}}};
const Names<RightStick> kRightSticks{
    {{RightStick::Camera, "camera"}, {RightStick::DPad, "dpad"}, {RightStick::Off, "off"}}};
const Names<TouchLayout> kTouchLayouts{{{TouchLayout::Psp, "psp"}, {TouchLayout::Action, "action"}}};
const Names<fast_forward::Mode> kFastForwardModes{
    {{fast_forward::Mode::Hold, "hold"}, {fast_forward::Mode::Toggle, "toggle"}, {fast_forward::Mode::Off, "off"}}};
const Names<NameEntry> kNameEntries{{{NameEntry::Keyboard, "keyboard"}, {NameEntry::Fixed, "fixed"}}};
const Names<FrameRate> kFrameRates{{{FrameRate::Fps30, "30"}, {FrameRate::Fps45, "45"}, {FrameRate::Fps60, "60"},
    {FrameRate::Fps90, "90"}, {FrameRate::Fps120, "120"}, {FrameRate::Display, "display"}}};

const Names<GpuCompat> kGpuCompats{{{GpuCompat::Auto, "auto"}, {GpuCompat::On, "on"}, {GpuCompat::Off, "off"}}};
const Names<UiTextures> kUiTextures{
    {{UiTextures::Off, "off"}, {UiTextures::Sharp, "sharp"}, {UiTextures::Mmpx, "mmpx"}}};

// Written by earlier versions: 1 typed the name into the window, which the
// on-screen keyboard now covers.
constexpr const char *kRetiredTypeNameKey = "input.type_name";
// Written by earlier versions: the gamepad's trigger profile, which control
// presets replaced. Read once to make the player's preset, then dropped.
constexpr const char *kRetiredTriggerProfileKey = "input.trigger_profile";
// settings.ini's format. Every save writes every key, defaults included, so
// once a default changes, a file must say which version wrote it for the
// player's choices to be told from the old defaults:
//   (none) up to v0.6.6;
//   2      Sharp text (text.crisp) off by default. v0.6.5 and v0.6.6 wrote
//          text.crisp=1 into every file they saved, chosen or not, so a file
//          without a version gets the new default.
constexpr const char *kVersionKey = "settings.version";
constexpr int kVersion = 2;
constexpr std::string_view kBindPrefix = "input.bind.";
constexpr std::string_view kPadPrefix = "input.pad.";

#define BOOL_FIELD(key, member)                                                                                        \
    Field {                                                                                                            \
        key, nullptr, [](Settings &s, const std::string &t) { return parse_bool(t, s.member); },                       \
            [](const Settings &s) { return std::string(s.member ? "1" : "0"); }, nullptr                               \
    }

// A layered armor part: "real" for the piece worn, or an armor piece's id.
bool parse_layered_piece(const std::string &text, std::int32_t &out) {
    if (text == "real") {
        out = kLayeredReal;
        return true;
    }
    // Not clamped: a number past the ids names no piece.
    std::uint32_t id = 0u;
    if (!parse_uint(text, 0u, 0xFFFFFFFFu, id) || id > static_cast<std::uint32_t>(kMaxLayeredPiece)) return false;
    out = static_cast<std::int32_t>(id);
    return true;
}
std::string format_layered_piece(std::int32_t id) {
    return id < 0 ? std::string("real") : std::to_string(id);
}

#define LAYERED_PIECE_FIELD(key, part)                                                                                 \
    Field {                                                                                                            \
        key, nullptr,                                                                                                  \
            [](Settings &s, const std::string &t) { return parse_layered_piece(t, s.layered_pieces[part]); },          \
            [](const Settings &s) { return format_layered_piece(s.layered_pieces[part]); }, nullptr                    \
    }

const std::vector<Field> &fields() {
    static const std::vector<Field> table = {
        {"video.internal_scale", "MHP3RD_INTERNAL_SCALE",
            [](Settings &s, const std::string &t) { return parse_scale(t, s.internal_scale); },
            [](const Settings &s) {
                return s.internal_scale == 0u ? std::string("auto") : std::to_string(s.internal_scale);
            },
            [](Settings &s, const char *t) {
                std::uint32_t value = s.internal_scale;
                if (parse_scale(t, value)) s.internal_scale = value;
            }},
        {"video.window_scale", nullptr,
            [](Settings &s, const std::string &t) { return parse_uint(t, 1u, kMaxWindowScale, s.window_scale); },
            [](const Settings &s) { return std::to_string(s.window_scale); }, nullptr},
        BOOL_FIELD("video.fullscreen", fullscreen),
        {"video.present_mode", nullptr,
            [](Settings &s, const std::string &t) { return kPresentModes.parse(t, s.present_mode); },
            [](const Settings &s) { return kPresentModes.format(s.present_mode); }, nullptr},
        // Written by earlier versions, which had Original and Stretch only.
        // Still written, so going back to one of them keeps the choice as
        // near as it can; video.aspect follows it and decides.
        {"video.keep_aspect", nullptr,
            [](Settings &s, const std::string &t) {
                bool keep = true;
                if (!parse_bool(t, keep)) return false;
                s.aspect = keep ? Aspect::Original : Aspect::Stretch;
                return true;
            },
            [](const Settings &s) { return std::string(s.aspect == Aspect::Stretch ? "0" : "1"); }, nullptr},
        {"video.aspect", nullptr, [](Settings &s, const std::string &t) { return kAspects.parse(t, s.aspect); },
            [](const Settings &s) { return kAspects.format(s.aspect); }, nullptr},
        BOOL_FIELD("video.sharp_screen", sharp_screen),
        BOOL_FIELD("video.sharp_textures", sharp_textures),
        {"video.texture_pack", "MHP3RD_TEXTURE_PACK",
            [](Settings &s, const std::string &t) { return parse_bool(t, s.texture_pack); },
            [](const Settings &s) { return std::string(s.texture_pack ? "1" : "0"); },
            // 0/off/no/false turn it off; anything else, a folder included, on.
            [](Settings &s, const char *t) { s.texture_pack = variable_flag(t); }},
        {"video.texture_pack_folder", nullptr,
            [](Settings &s, const std::string &t) {
                s.texture_pack_folder = t;
                return true;
            },
            [](const Settings &s) { return s.texture_pack_folder; }, nullptr},
        {"video.unthrottled", "MHP3RD_UNTHROTTLED",
            [](Settings &s, const std::string &t) { return parse_bool(t, s.unthrottled); },
            [](const Settings &s) { return std::string(s.unthrottled ? "1" : "0"); },
            [](Settings &s, const char *t) { s.unthrottled = variable_present(t); }},
        {"video.fast_loading", "MHP3RD_FAST_LOADING",
            [](Settings &s, const std::string &t) { return parse_bool(t, s.fast_loading); },
            [](const Settings &s) { return std::string(s.fast_loading ? "1" : "0"); },
            [](Settings &s, const char *t) { s.fast_loading = variable_flag(t); }},
        {"video.fast_forward", "MHP3RD_FAST_FORWARD",
            [](Settings &s, const std::string &t) { return kFastForwardModes.parse(t, s.fast_forward); },
            [](const Settings &s) { return kFastForwardModes.format(s.fast_forward); },
            [](Settings &s, const char *t) {
                // hold, toggle or off; 0, no and false also turn it off.
                if (!kFastForwardModes.parse(t, s.fast_forward))
                    s.fast_forward = variable_flag(t) ? fast_forward::Mode::Hold : fast_forward::Mode::Off;
            }},
        {"video.fast_forward_speed", "MHP3RD_FAST_FORWARD_SPEED",
            [](Settings &s, const std::string &t) {
                return parse_uint(t, fast_forward::kMinSpeed, fast_forward::kMaxSpeed, s.fast_forward_speed);
            },
            [](const Settings &s) { return std::to_string(s.fast_forward_speed); },
            [](Settings &s, const char *t) {
                std::uint32_t value = s.fast_forward_speed;
                if (parse_uint(t, fast_forward::kMinSpeed, fast_forward::kMaxSpeed, value))
                    s.fast_forward_speed = value;
            }},
        {"video.frame_rate", "MHP3RD_FRAME_RATE",
            [](Settings &s, const std::string &t) { return kFrameRates.parse(t, s.frame_rate); },
            [](const Settings &s) { return kFrameRates.format(s.frame_rate); },
            [](Settings &s, const char *t) {
                if (!kFrameRates.parse(t, s.frame_rate)) s.frame_rate = FrameRate::Fps30;
            }},
        {"video.frame_rate_auto", "MHP3RD_FRAME_RATE_AUTO",
            [](Settings &s, const std::string &t) { return parse_bool(t, s.frame_rate_auto); },
            [](const Settings &s) { return std::string(s.frame_rate_auto ? "1" : "0"); },
            [](Settings &s, const char *t) { s.frame_rate_auto = variable_flag(t); }},
        {"video.performance", "MHP3RD_PERF",
            [](Settings &s, const std::string &t) { return kPerfDisplays.parse(t, s.perf); },
            [](const Settings &s) { return kPerfDisplays.format(s.perf); },
            [](Settings &s, const char *t) {
                // `1` has always meant the overlay and the log, `log` the log only.
                if (std::strcmp(t, "log") == 0)
                    s.perf = PerfDisplay::Log;
                else
                    s.perf = *t != '\0' && variable_flag(t) ? PerfDisplay::OverlayAndLog : PerfDisplay::Off;
            }},
        {"video.gpu_compat", "MHP3RD_GPU_COMPAT",
            [](Settings &s, const std::string &t) { return kGpuCompats.parse(t, s.gpu_compat); },
            [](const Settings &s) { return kGpuCompats.format(s.gpu_compat); },
            [](Settings &s, const char *t) {
                // 1/on and 0/off as for the other switches; auto as in the file.
                bool on = false;
                if (kGpuCompats.parse(t, s.gpu_compat)) return;
                if (parse_bool(t, on)) s.gpu_compat = on ? GpuCompat::On : GpuCompat::Off;
            }},
        {"text.font", "MHP3RD_FONT",
            [](Settings &s, const std::string &t) {
                s.font = t;
                return true;
            },
            [](const Settings &s) { return s.font; }, [](Settings &s, const char *t) { s.font = t; }},
        {"text.crisp", "MHP3RD_CRISP_TEXT",
            [](Settings &s, const std::string &t) { return parse_bool(t, s.crisp_text); },
            [](const Settings &s) { return std::string(s.crisp_text ? "1" : "0"); },
            [](Settings &s, const char *t) { s.crisp_text = variable_flag(t); }},
        {"video.ui_textures", "MHP3RD_UI_TEXTURES",
            [](Settings &s, const std::string &t) { return kUiTextures.parse(t, s.ui_textures); },
            [](const Settings &s) { return kUiTextures.format(s.ui_textures); },
            [](Settings &s, const char *t) {
                if (!kUiTextures.parse(t, s.ui_textures)) s.ui_textures = UiTextures::Off;
            }},
        {"text.weight", nullptr,
            [](Settings &s, const std::string &t) { return parse_uint(t, 0u, kMaxFontWeight, s.font_weight); },
            [](const Settings &s) { return std::to_string(s.font_weight); }, nullptr},
        {"audio.volume", nullptr, [](Settings &s, const std::string &t) { return parse_uint(t, 0u, 100u, s.volume); },
            [](const Settings &s) { return std::to_string(s.volume); }, nullptr},
        BOOL_FIELD("audio.mute", mute),
        BOOL_FIELD("audio.background_mute", background_mute),
        {"input.confirm", "MHP3RD_PAD_FACE",
            [](Settings &s, const std::string &t) {
                if (t == "south")
                    s.confirm_south = true;
                else if (t == "east")
                    s.confirm_south = false;
                else
                    return false;
                return true;
            },
            [](const Settings &s) { return std::string(s.confirm_south ? "south" : "east"); },
            [](Settings &s, const char *t) {
                s.confirm_south = std::strcmp(t, "xbox") == 0 || std::strcmp(t, "south") == 0;
            }},
        {"input.dead_zone", "MHP3RD_PAD_DEADZONE",
            [](Settings &s, const std::string &t) { return parse_float(t, 0.0f, 0.9f, s.dead_zone); },
            [](const Settings &s) { return format_float(s.dead_zone); },
            [](Settings &s, const char *t) { s.dead_zone = variable_float(t, 0.15f, 0.0f, 0.9f); }},
        {"input.trigger", "MHP3RD_PAD_TRIGGER",
            [](Settings &s, const std::string &t) { return parse_float(t, 0.05f, 1.0f, s.trigger); },
            [](const Settings &s) { return format_float(s.trigger); },
            [](Settings &s, const char *t) { s.trigger = variable_float(t, 0.25f, 0.05f, 1.0f); }},
        {"input.chord_window", "MHP3RD_CHORD_WINDOW",
            [](Settings &s, const std::string &t) {
                return parse_uint(t, 0u, input::kMaxChordWindowMs, s.chord_window);
            },
            [](const Settings &s) { return std::to_string(s.chord_window); },
            [](Settings &s, const char *t) {
                if (!parse_uint(t, 0u, input::kMaxChordWindowMs, s.chord_window))
                    s.chord_window = variable_flag(t) ? input::kDefaultChordWindowMs : 0u;
            }},
        {"input.preset", "MHP3RD_CONTROL_PRESET",
            [](Settings &s, const std::string &t) {
                const std::optional<input::PresetChoice> choice = input::parse_choice(t);
                if (!choice) return false;
                s.control_preset = *choice;
                return true;
            },
            [](const Settings &s) { return input::format(s.control_preset); },
            [](Settings &s, const char *t) {
                // A shipped preset's id, or the name of one of the player's.
                if (const std::optional<input::Preset> shipped = input::preset_from_id(t))
                    s.control_preset = {shipped, {}};
                else if (const std::optional<input::PresetChoice> choice = input::parse_choice(t))
                    s.control_preset = *choice;
                else
                    s.control_preset = {std::nullopt, t};
            }},
        {"input.move_stick", nullptr,
            [](Settings &s, const std::string &t) {
                if (t == "left")
                    s.controls.swap_sticks = false;
                else if (t == "right")
                    s.controls.swap_sticks = true;
                else
                    return false;
                return true;
            },
            [](const Settings &s) { return std::string(s.controls.swap_sticks ? "right" : "left"); }, nullptr},
        {"input.right_stick", "MHP3RD_PAD_RSTICK_DPAD",
            [](Settings &s, const std::string &t) { return kRightSticks.parse(t, s.right_stick); },
            [](const Settings &s) { return kRightSticks.format(s.right_stick); },
            [](Settings &s, const char *t) {
                s.right_stick = variable_flag(t) ? RightStick::DPad : RightStick::Camera;
            }},
        {"input.right_stick_zone", "MHP3RD_PAD_RSTICK_ZONE",
            [](Settings &s, const std::string &t) { return parse_float(t, 0.1f, 1.0f, s.right_stick_zone); },
            [](const Settings &s) { return format_float(s.right_stick_zone); },
            [](Settings &s, const char *t) { s.right_stick_zone = variable_float(t, 0.5f, 0.1f, 1.0f); }},
        {"input.analog_camera", "MHP3RD_ANALOG_CAMERA",
            [](Settings &s, const std::string &t) { return parse_bool(t, s.analog_camera); },
            [](const Settings &s) { return std::string(s.analog_camera ? "1" : "0"); },
            [](Settings &s, const char *t) { s.analog_camera = variable_flag(t); }},
        {"input.lock_on", "MHP3RD_LOCK_ON", [](Settings &s, const std::string &t) { return parse_bool(t, s.lock_on); },
            [](const Settings &s) { return std::string(s.lock_on ? "1" : "0"); },
            [](Settings &s, const char *t) { s.lock_on = variable_flag(t); }},
        BOOL_FIELD("input.lock_on_marker", lock_on_marker),
        {"input.camera_speed", "MHP3RD_CAMERA_SPEED",
            [](Settings &s, const std::string &t) { return parse_float(t, 20.0f, 720.0f, s.camera_speed); },
            [](const Settings &s) { return format_float(s.camera_speed); },
            [](Settings &s, const char *t) { s.camera_speed = variable_float(t, 190.0f, 20.0f, 720.0f); }},
        {"input.aim_speed", "MHP3RD_AIM_SPEED",
            [](Settings &s, const std::string &t) { return parse_float(t, 10.0f, 360.0f, s.aim_speed); },
            [](const Settings &s) { return format_float(s.aim_speed); },
            [](Settings &s, const char *t) { s.aim_speed = variable_float(t, 90.0f, 10.0f, 360.0f); }},
        BOOL_FIELD("input.invert_camera_x", invert_camera_x),
        BOOL_FIELD("input.invert_camera_y", invert_camera_y),
        {"input.mouse", "MHP3RD_MOUSE", [](Settings &s, const std::string &t) { return parse_bool(t, s.mouse); },
            [](const Settings &s) { return std::string(s.mouse ? "1" : "0"); },
            [](Settings &s, const char *t) { s.mouse = variable_flag(t); }},
        {"input.mouse_sensitivity", "MHP3RD_MOUSE_SENSITIVITY",
            [](Settings &s, const std::string &t) {
                return parse_float(t, kMinMouseSensitivity, kMaxMouseSensitivity, s.mouse_sensitivity);
            },
            [](const Settings &s) { return format_float(s.mouse_sensitivity); },
            [](Settings &s, const char *t) {
                s.mouse_sensitivity = variable_float(t, 0.10f, kMinMouseSensitivity, kMaxMouseSensitivity);
            }},
        BOOL_FIELD("input.touch_controls", touch_controls),
        BOOL_FIELD("input.touch_dpad", touch_dpad),
        {"input.touch_opacity", nullptr,
            [](Settings &s, const std::string &t) {
                return parse_float(t, kMinTouchOpacity, kMaxTouchOpacity, s.touch_opacity);
            },
            [](const Settings &s) { return format_float(s.touch_opacity); }, nullptr},
        {"input.touch_size", nullptr,
            [](Settings &s, const std::string &t) {
                return parse_float(t, kMinTouchSize, kMaxTouchSize, s.touch_size);
            },
            [](const Settings &s) { return format_float(s.touch_size); }, nullptr},
        {"input.touch_camera_speed", nullptr,
            [](Settings &s, const std::string &t) {
                return parse_float(t, kMinTouchCameraSpeed, kMaxTouchCameraSpeed, s.touch_camera_speed);
            },
            [](const Settings &s) { return format_float(s.touch_camera_speed); }, nullptr},
        {"input.touch_layout", "MHP3RD_TOUCH_LAYOUT",
            [](Settings &s, const std::string &t) { return kTouchLayouts.parse(t, s.touch_layout); },
            [](const Settings &s) { return kTouchLayouts.format(s.touch_layout); },
            [](Settings &s, const char *t) {
                if (!kTouchLayouts.parse(t, s.touch_layout))
                    std::cerr << "[settings] MHP3RD_TOUCH_LAYOUT: psp or action\n";
            }},
        BOOL_FIELD("input.touch_haptics", touch_haptics),
        BOOL_FIELD("input.background_gamepad", background_gamepad),
        BOOL_FIELD("input.invert_mouse_x", invert_mouse_x),
        BOOL_FIELD("input.invert_mouse_y", invert_mouse_y),
        {"input.name_entry", "MHP3RD_OSK_MODE",
            [](Settings &s, const std::string &t) { return kNameEntries.parse(t, s.name_entry); },
            [](const Settings &s) { return kNameEntries.format(s.name_entry); },
            [](Settings &s, const char *t) {
                if (!kNameEntries.parse(t, s.name_entry))
                    std::cerr << "[settings] MHP3RD_OSK_MODE: keyboard or fixed\n";
            }},
        {"input.name", "MHP3RD_OSK_TEXT",
            [](Settings &s, const std::string &t) {
                if (t.empty()) return false;
                s.name = t;
                return true;
            },
            [](const Settings &s) { return s.name; }, [](Settings &s, const char *t) { s.name = t; }},
        {"network.adhoc", "MHP3RD_ADHOC", [](Settings &s, const std::string &t) { return parse_bool(t, s.adhoc); },
            [](const Settings &s) { return std::string(s.adhoc ? "1" : "0"); },
            [](Settings &s, const char *t) { s.adhoc = variable_flag(t); }},
        {"network.server", "MHP3RD_ADHOC_SERVER",
            [](Settings &s, const std::string &t) {
                s.adhoc_server = t;
                return true;
            },
            [](const Settings &s) { return s.adhoc_server; }, [](Settings &s, const char *t) { s.adhoc_server = t; }},
        {"network.nickname", "MHP3RD_ADHOC_NICKNAME",
            [](Settings &s, const std::string &t) {
                s.adhoc_nickname = t;
                return true;
            },
            [](const Settings &s) { return s.adhoc_nickname; },
            [](Settings &s, const char *t) { s.adhoc_nickname = t; }},
        {"network.mac", "MHP3RD_ADHOC_MAC",
            [](Settings &s, const std::string &t) {
                s.adhoc_mac = t;
                return true;
            },
            [](const Settings &s) { return s.adhoc_mac; }, [](Settings &s, const char *t) { s.adhoc_mac = t; }},
        {"ui.menu_pause", "MHP3RD_MENU_PAUSE",
            [](Settings &s, const std::string &t) { return parse_bool(t, s.menu_pause); },
            [](const Settings &s) { return std::string(s.menu_pause ? "1" : "0"); },
            [](Settings &s, const char *t) { s.menu_pause = variable_flag(t); }},
        {"ui.menu_pause_multiplayer", "MHP3RD_MENU_PAUSE_MULTIPLAYER",
            [](Settings &s, const std::string &t) { return parse_bool(t, s.menu_pause_multiplayer); },
            [](const Settings &s) { return std::string(s.menu_pause_multiplayer ? "1" : "0"); },
            [](Settings &s, const char *t) { s.menu_pause_multiplayer = variable_flag(t); }},
        {"network.recent", nullptr,
            [](Settings &s, const std::string &t) {
                s.adhoc_recent.clear();
                std::size_t start = 0;
                while (start <= t.size()) {
                    const std::size_t end = std::min(t.find(',', start), t.size());
                    if (end > start) s.adhoc_recent.push_back(t.substr(start, end - start));
                    start = end + 1u;
                }
                return true;
            },
            [](const Settings &s) {
                std::string text;
                for (const std::string &address : s.adhoc_recent) text += (text.empty() ? "" : ",") + address;
                return text;
            },
            nullptr},
        {"network.host_port", "MHP3RD_ADHOC_HOST_PORT",
            [](Settings &s, const std::string &t) { return parse_uint(t, 1024u, 65534u, s.adhoc_host_port); },
            [](const Settings &s) { return std::to_string(s.adhoc_host_port); },
            [](Settings &s, const char *t) {
                std::uint32_t value = s.adhoc_host_port;
                if (parse_uint(t, 1024u, 65534u, value)) s.adhoc_host_port = value;
            }},
        {"look.layered_armor", "MHP3RD_LAYERED_ARMOR",
            [](Settings &s, const std::string &t) { return parse_bool(t, s.layered_armor); },
            [](const Settings &s) { return std::string(s.layered_armor ? "1" : "0"); },
            [](Settings &s, const char *t) { s.layered_armor = variable_flag(t); }},
        BOOL_FIELD("look.layered_all", layered_all),
        LAYERED_PIECE_FIELD("look.layered_chest", 0),
        LAYERED_PIECE_FIELD("look.layered_arms", 1),
        LAYERED_PIECE_FIELD("look.layered_waist", 2),
        LAYERED_PIECE_FIELD("look.layered_legs", 3),
        LAYERED_PIECE_FIELD("look.layered_head", 4),
        {"experimental.free_camera", "MHP3RD_FREE_CAMERA",
            [](Settings &s, const std::string &t) { return parse_bool(t, s.free_camera); },
            [](const Settings &s) { return std::string(s.free_camera ? "1" : "0"); },
            [](Settings &s, const char *t) { s.free_camera = variable_flag(t); }},
        {"experimental.free_camera_speed", "MHP3RD_FREE_CAMERA_SPEED",
            [](Settings &s, const std::string &t) {
                return parse_float(t, kMinFreeCameraSpeed, kMaxFreeCameraSpeed, s.free_camera_speed);
            },
            [](const Settings &s) { return format_float(s.free_camera_speed); },
            [](Settings &s, const char *t) {
                s.free_camera_speed = variable_float(t, 400.0f, kMinFreeCameraSpeed, kMaxFreeCameraSpeed);
            }},
        {"experimental.free_camera_hide_hud", "MHP3RD_FREE_CAMERA_HIDE_HUD",
            [](Settings &s, const std::string &t) { return parse_bool(t, s.free_camera_hide_hud); },
            [](const Settings &s) { return std::string(s.free_camera_hide_hud ? "1" : "0"); },
            [](Settings &s, const char *t) { s.free_camera_hide_hud = variable_flag(t); }},
        BOOL_FIELD("ui.menu_hint_seen", menu_hint_seen),
        BOOL_FIELD("saves.backup_timestamp", backup_timestamp),
        BOOL_FIELD("saves.backup_reminder", backup_reminder),
        {"saves.backup_reminded", nullptr,
            [](Settings &s, const std::string &t) {
                s.backup_reminded = t;
                return true;
            },
            [](const Settings &s) { return s.backup_reminded; }, nullptr},
        {"ui.last_folder", nullptr,
            [](Settings &s, const std::string &t) {
                s.last_folder = t;
                return true;
            },
            [](const Settings &s) { return s.last_folder; }, nullptr},
        {"ui.menu_tab", nullptr,
            [](Settings &s, const std::string &t) {
                s.menu_tab = t;
                return true;
            },
            [](const Settings &s) { return s.menu_tab; }, nullptr},
    };
    return table;
}

// Two keys per bound action after the fixed table, one for the keyboard and
// the mouse and one for gamepads: "input.bind.triangle=Mouse Left",
// "input.pad.triangle=Pad North".
const std::vector<Field> &all_fields() {
    static const std::vector<Field> table = [] {
        // Field keys are C strings; these hold them for the program's life.
        static std::vector<std::string> keys(input::kActions * 2u + input::touch::kElements);
        std::vector<Field> list = fields();
        for (std::size_t i = 0; i < input::kActions; ++i) {
            keys[i] = std::string(kBindPrefix) + input::info(static_cast<input::Action>(i)).key;
            list.push_back(Field{keys[i].c_str(), nullptr,
                [i](Settings &s, const std::string &t) { return input::parse(t, s.controls.keys[i]); },
                [i](const Settings &s) { return input::format(s.controls.keys[i]); }, nullptr});
        }
        for (std::size_t i = 0; i < input::kActions; ++i) {
            std::string &key = keys[input::kActions + i];
            key = std::string(kPadPrefix) + input::info(static_cast<input::Action>(i)).key;
            list.push_back(Field{key.c_str(), nullptr,
                [i](Settings &s, const std::string &t) { return input::parse(t, s.controls.pad[i]); },
                [i](const Settings &s) { return input::format(s.controls.pad[i]); }, nullptr});
        }
        // The action layout's elements: "input.touch_action.attack=right 0.330 0.760 0.090 0x1000 1".
        for (std::size_t i = 0; i < input::touch::kElements; ++i) {
            std::string &key = keys[input::kActions * 2u + i];
            key = std::string("input.touch_action.") + input::touch::info(static_cast<input::touch::Element>(i)).key;
            list.push_back(Field{key.c_str(), nullptr,
                [i](Settings &s, const std::string &t) { return input::touch::parse(t, s.touch_action.elements[i]); },
                [i](const Settings &s) { return input::touch::format(s.touch_action.elements[i]); }, nullptr});
        }
        return list;
    }();
    return table;
}

// The player's presets: "input.user_preset.<n>.name", ".move_stick",
// ".bind.<action>" and ".pad.<action>", numbered from 1 in the menu's order.
constexpr std::string_view kUserPresetPrefix = "input.user_preset.";
// The player's own actions (#198): "input.combo.<n>.buttons=L + Square",
// ".bind" and ".pad", numbered from 1; a preset's under its prefix.
constexpr std::string_view kComboPrefix = "input.combo.";

// Reads one combo's field into `combos`, numbered from 1: `field` is
// "<n>.buttons", "<n>.bind" or "<n>.pad".
void read_combo_field(std::map<unsigned long, input::Combo> &combos, const std::string &field, const std::string &value,
    const std::string &key) {
    char *end = nullptr;
    const unsigned long number = std::strtoul(field.c_str(), &end, 10);
    if (end == field.c_str() || *end != '.' || number == 0u) return;
    const std::string part = end + 1;
    input::Combo &combo = combos[number];
    bool ok = true;
    if (part == "buttons")
        ok = input::parse_buttons(value, combo.buttons);
    else if (part == "bind")
        ok = input::parse(value, combo.keys);
    else if (part == "pad")
        ok = input::parse(value, combo.pad);
    if (!ok) std::cerr << "[settings] ignoring " << key << "=" << value << "\n";
}

// The combos read, in their numbers' order, without any that press nothing.
std::vector<input::Combo> settle_combos(const std::map<unsigned long, input::Combo> &found) {
    std::vector<input::Combo> combos;
    for (const auto &[number, combo] : found)
        if ((combo.buttons & input::kComboButtons) != 0u && combos.size() < input::kMaxCombos) combos.push_back(combo);
    return combos;
}

std::vector<input::Combo> read_combos(const Entries &entries) {
    std::map<unsigned long, input::Combo> found;
    for (auto it = entries.lower_bound(std::string(kComboPrefix));
        it != entries.end() && it->first.starts_with(kComboPrefix); ++it)
        read_combo_field(found, it->first.substr(kComboPrefix.size()), it->second, it->first);
    return settle_combos(found);
}

void write_combos(const std::vector<input::Combo> &combos, const std::string &prefix, Entries &entries) {
    for (auto it = entries.lower_bound(prefix); it != entries.end() && it->first.starts_with(prefix);)
        it = entries.erase(it);
    std::size_t number = 0;
    for (std::size_t n = 0; n < combos.size(); ++n) {
        // One whose buttons are still being chosen is not kept yet.
        if ((combos[n].buttons & input::kComboButtons) == 0u) continue;
        const std::string at = prefix + std::to_string(++number) + ".";
        entries[at + "buttons"] = input::format_buttons(combos[n].buttons);
        entries[at + "bind"] = input::format(combos[n].keys);
        entries[at + "pad"] = input::format(combos[n].pad);
    }
}

// Actions added since a layout was written (the item bar's, #198, and
// lock-on, #163), which the file has no key for, take what the shipped preset `from` has for them,
// each chord only if it clashes with nothing the layout has: settings from
// before keep every binding they had. Other missing keys are left as they
// always were.
void fill_new_actions(
    input::Layout &layout, const input::Layout &from, const std::function<bool(std::size_t, bool)> &written) {
    static constexpr input::Action kAdded[] = {
        input::Action::ItemLeft, input::Action::ItemRight, input::Action::LockOn};
    for (const bool pad : {false, true}) {
        input::Bindings &table = pad ? layout.pad : layout.keys;
        const input::Bindings &defaults = pad ? from.pad : from.keys;
        for (const input::Action added : kAdded) {
            const auto i = static_cast<std::size_t>(added);
            if (written(i, pad)) continue;
            table[i] = {};
            for (const input::Chord &c : defaults[i]) {
                if (c.empty() || !input::add(table[i], c)) continue;
                const input::Table view{table, layout.combos, pad};
                if (!input::conflicts(view, i).empty()) input::remove(table[i], c);
            }
        }
    }
}

std::vector<input::UserPreset> read_user_presets(const Entries &entries) {
    std::map<unsigned long, input::UserPreset> found;
    std::map<unsigned long, bool> named;
    std::map<unsigned long, std::map<unsigned long, input::Combo>> combos;
    for (auto it = entries.lower_bound(std::string(kUserPresetPrefix));
        it != entries.end() && it->first.starts_with(kUserPresetPrefix); ++it) {
        const std::string rest = it->first.substr(kUserPresetPrefix.size());
        char *end = nullptr;
        const unsigned long number = std::strtoul(rest.c_str(), &end, 10);
        if (end == rest.c_str() || *end != '.') continue;
        const std::string field = end + 1;
        input::UserPreset &preset = found[number];
        if (field == "name") {
            preset.name = input::clean_preset_name(it->second);
            named[number] = !preset.name.empty();
        } else if (field == "move_stick") {
            preset.layout.swap_sticks = it->second == "right";
        } else if (field == "base") {
            if (const std::optional<input::Preset> base = input::preset_from_id(it->second)) preset.base = *base;
        } else if (field.starts_with("combo.")) {
            read_combo_field(combos[number], field.substr(6), it->second, it->first);
        } else {
            const bool keys = field.starts_with(kBindPrefix.substr(6));
            const bool pad = field.starts_with(kPadPrefix.substr(6));
            if (!keys && !pad) continue;
            const std::string action = field.substr(field.find('.') + 1u);
            for (std::size_t i = 0; i < input::kActions; ++i) {
                if (action != input::info(static_cast<input::Action>(i)).key) continue;
                input::Slots &slots = keys ? preset.layout.keys[i] : preset.layout.pad[i];
                if (!input::parse(it->second, slots))
                    std::cerr << "[settings] ignoring " << it->first << "=" << it->second << "\n";
            }
        }
    }
    std::vector<input::UserPreset> presets;
    for (auto &[number, preset] : found) {
        if (!named[number] || presets.size() == input::kMaxUserPresets) continue;
        preset.layout.combos = settle_combos(combos[number]);
        const std::string prefix = std::string(kUserPresetPrefix) + std::to_string(number) + ".";
        fill_new_actions(preset.layout, input::layout(preset.base), [&](std::size_t i, bool pad) {
            return entries.count(prefix + (pad ? "pad." : "bind.") + input::info(static_cast<input::Action>(i)).key) !=
                0u;
        });
        bool duplicate = false;
        for (const input::UserPreset &other : presets) duplicate = duplicate || other.name == preset.name;
        if (!duplicate) presets.push_back(std::move(preset));
    }
    return presets;
}

void write_user_presets(const std::vector<input::UserPreset> &presets, Entries &entries) {
    for (auto it = entries.lower_bound(std::string(kUserPresetPrefix));
        it != entries.end() && it->first.starts_with(kUserPresetPrefix);)
        it = entries.erase(it);
    for (std::size_t n = 0; n < presets.size(); ++n) {
        const std::string prefix = std::string(kUserPresetPrefix) + std::to_string(n + 1u) + ".";
        const input::UserPreset &preset = presets[n];
        entries[prefix + "name"] = preset.name;
        entries[prefix + "move_stick"] = preset.layout.swap_sticks ? "right" : "left";
        entries[prefix + "base"] = input::info(preset.base).id;
        for (std::size_t i = 0; i < input::kActions; ++i) {
            const char *action = input::info(static_cast<input::Action>(i)).key;
            entries[prefix + "bind." + action] = input::format(preset.layout.keys[i]);
            entries[prefix + "pad." + action] = input::format(preset.layout.pad[i]);
        }
        write_combos(preset.layout.combos, prefix + "combo.", entries);
    }
}

// The controls once every key is read: earlier versions' become a preset,
// and the layout in use is always one of the presets.
void settle_controls(Settings &s, const Entries &entries) {
    s.user_presets = read_user_presets(entries);
    s.controls.combos = read_combos(entries);
    if (entries.count("input.preset") != 0u) {
        // The shipped preset the layout in use is based on.
        input::Preset base = s.control_preset.shipped.value_or(input::Preset::Default);
        if (!s.control_preset.shipped)
            if (const input::UserPreset *preset = find_user_preset(s, s.control_preset.user)) base = preset->base;
        fill_new_actions(s.controls, input::layout(base), [&](std::size_t i, bool pad) {
            return entries.count(std::string(pad ? kPadPrefix : kBindPrefix) +
                       input::info(static_cast<input::Action>(i)).key) != 0u;
        });
    }
    if (entries.count("input.preset") == 0u) {
        const auto profile = entries.find(kRetiredTriggerProfileKey);
        s.controls =
            input::layout_from_earlier(s.controls.keys, profile != entries.end() ? profile->second : "standard");
        s.control_preset = {input::Preset::Default, {}};
    }
    // The layout in use decides: a preset that differs from it, changed by
    // hand or by an earlier version writing input.bind.*, gives way to it.
    if (s.control_preset.shipped) {
        if (input::layout(*s.control_preset.shipped) == s.controls) return;
        if (const std::optional<input::Preset> same = input::matching_preset(s.controls)) {
            s.control_preset = {same, {}};
            return;
        }
        const std::string name = input::unique_preset_name(s.user_presets, "Custom");
        const input::UserPreset made{name, s.controls, *s.control_preset.shipped};
        if (s.user_presets.size() < input::kMaxUserPresets)
            s.user_presets.push_back(made);
        else
            s.user_presets.back() = made;
        s.control_preset = {std::nullopt, name};
        return;
    }
    if (input::UserPreset *preset = find_user_preset(s, s.control_preset.user)) {
        preset->layout = s.controls;
        return;
    }
    const std::string name = input::clean_preset_name(s.control_preset.user).empty()
        ? input::unique_preset_name(s.user_presets, "Custom")
        : input::clean_preset_name(s.control_preset.user);
    if (s.user_presets.size() == input::kMaxUserPresets) s.user_presets.pop_back();
    s.user_presets.push_back({name, s.controls});
    s.control_preset = {std::nullopt, name};
}

#undef BOOL_FIELD

struct State {
    bool loaded{};
    Settings values;
    std::filesystem::path data_dir;
    // What settings.ini held, so values the environment decided are written
    // back as the file had them.
    install::SettingsEntries file;
    std::map<std::string, const char *> overrides;
};

State &state() {
    static State value;
    return value;
}

void write_entries(const Settings &values, Entries &entries) {
    for (const Field &field : all_fields()) entries[field.key] = field.format(values);
    entries[kVersionKey] = std::to_string(kVersion);
    write_combos(values.controls.combos, std::string(kComboPrefix), entries);
    write_user_presets(values.user_presets, entries);
    entries.erase(kRetiredTypeNameKey);
    entries.erase(kRetiredTriggerProfileKey);
}

// Drops what a file an earlier version wrote holds only because that
// version wrote its defaults (kVersion). True when something was dropped.
bool upgrade(Entries &entries) {
    std::uint32_t version = 1u;
    if (const auto found = entries.find(kVersionKey); found != entries.end())
        (void)parse_uint(found->second, 1u, 1000u, version);
    if (version >= 2u) return false;
    return entries.erase("text.crisp") != 0u;
}

void read_entries(Settings &values, const Entries &entries) {
    for (const Field &field : all_fields())
        if (const auto found = entries.find(field.key); found != entries.end() && !field.parse(values, found->second))
            std::cerr << "[settings] ignoring " << field.key << "=" << found->second << "\n";
    settle_controls(values, entries);
}

void load(State &s) {
    s.loaded = true;
    s.values = defaults();
    try {
        s.data_dir = install::user_data_directory();
        s.file = install::read_settings_file(s.data_dir);
    } catch (const std::exception &e) {
        std::cerr << "[settings] cannot read settings.ini: " << e.what() << "\n";
    }
    // Dropped from what the file held too, so a run whose environment
    // decides Sharp text does not write the old default back as chosen.
    if (upgrade(s.file))
        std::cout << "[settings] text.crisp from an earlier version dropped: Sharp text is off by default now; "
                     "turn it on in Video if you want it\n";
    read_entries(s.values, s.file);
    for (const Field &field : all_fields()) {
        if (field.variable == nullptr) continue;
        // UTF-8, like settings.ini: MHP3RD_FONT and the folders are paths.
        const std::optional<std::string> text = environment_utf8(field.variable);
        if (!text) continue;
        const input::PresetChoice before = s.values.control_preset;
        field.parse_variable(s.values, text->c_str());
        s.overrides[field.key] = field.variable;
        // A preset chosen for the run is put in use, and not saved.
        if (std::string_view(field.key) == "input.preset" && !choose_preset(s.values, s.values.control_preset)) {
            std::cerr << "[settings] " << field.variable << ": no preset \"" << *text << "\"\n";
            s.values.control_preset = before;
        }
    }
    if (std::getenv("MHP3RD_PAD_TRIGGERS") != nullptr)
        std::cerr << "[settings] MHP3RD_PAD_TRIGGERS is retired: choose a control preset (MHP3RD_CONTROL_PRESET, "
                     "or Controls in the menu)\n";
    // A fixed name in the environment is meant for unattended runs, which
    // nobody is there to type in, so it also answers at once unless
    // MHP3RD_OSK_MODE says otherwise.
    if (s.overrides.count("input.name_entry") == 0u && std::getenv("MHP3RD_OSK_TEXT") != nullptr) {
        s.values.name_entry = NameEntry::Fixed;
        s.overrides["input.name_entry"] = "MHP3RD_OSK_TEXT";
    }
}

} // namespace

Settings &current() {
    State &s = state();
    if (!s.loaded) load(s);
    return s.values;
}

Settings defaults_for(Platform platform) {
    Settings values{};
    if (platform == Platform::Android) {
        values.aspect = Aspect::Fill;
        values.fullscreen = true;
        values.mouse = false;
    }
    return values;
}

const Settings &defaults() {
    static const Settings value = defaults_for(kPlatform);
    return value;
}

void save() {
    State &s = state();
    if (!s.loaded) load(s);
    install::SettingsEntries entries;
    try {
        // Re-read, so a key the installer wrote since start-up survives.
        entries = install::read_settings_file(s.data_dir);
        write_entries(s.values, entries);
        // A preset chosen by the environment leaves the file's controls.
        if (s.overrides.count("input.preset") != 0u) {
            for (const Field &field : all_fields()) {
                const std::string_view key = field.key;
                if (key != "input.preset" && key != "input.move_stick" && !key.starts_with(kBindPrefix) &&
                    !key.starts_with(kPadPrefix))
                    continue;
                if (const auto kept = s.file.find(field.key); kept != s.file.end())
                    entries[field.key] = kept->second;
                else
                    entries.erase(field.key);
            }
        }
        for (const Field &field : all_fields()) {
            if (s.overrides.count(field.key) == 0u) continue;
            const auto kept = s.file.find(field.key);
            if (kept != s.file.end())
                entries[field.key] = kept->second;
            else
                entries.erase(field.key);
        }
        install::write_settings_file(s.data_dir, entries);
    } catch (const std::exception &e) {
        std::cerr << "[settings] cannot write settings.ini: " << e.what() << "\n";
    }
}

input::UserPreset *find_user_preset(Settings &settings, const std::string &name) {
    for (input::UserPreset &preset : settings.user_presets)
        if (preset.name == name) return &preset;
    return nullptr;
}

bool choose_preset(Settings &settings, const input::PresetChoice &choice) {
    if (choice.shipped) {
        settings.controls = input::layout(*choice.shipped);
    } else {
        const input::UserPreset *preset = find_user_preset(settings, choice.user);
        if (preset == nullptr) return false;
        settings.controls = preset->layout;
    }
    settings.control_preset = choice;
    return true;
}

std::optional<std::string> prepare_controls_edit(Settings &settings) {
    if (!settings.control_preset.shipped) return std::nullopt;
    if (settings.user_presets.size() >= input::kMaxUserPresets) settings.user_presets.pop_back();
    const std::string name = input::unique_preset_name(settings.user_presets, "Custom");
    settings.user_presets.push_back({name, settings.controls, *settings.control_preset.shipped});
    settings.control_preset = {std::nullopt, name};
    return name;
}

input::Preset base_preset(const Settings &settings) {
    if (settings.control_preset.shipped) return *settings.control_preset.shipped;
    for (const input::UserPreset &preset : settings.user_presets)
        if (preset.name == settings.control_preset.user) return preset.base;
    return input::Preset::Default;
}

void controls_edited(Settings &settings) {
    if (settings.control_preset.shipped) return;
    if (input::UserPreset *preset = find_user_preset(settings, settings.control_preset.user))
        preset->layout = settings.controls;
}

Settings from_entries(const Entries &entries) {
    Settings values = defaults();
    Entries upgraded = entries;
    (void)upgrade(upgraded);
    read_entries(values, upgraded);
    return values;
}

Entries to_entries(const Settings &settings) {
    Entries entries;
    write_entries(settings, entries);
    return entries;
}

const char *overridden_by(const char *key) {
    State &s = state();
    if (!s.loaded) load(s);
    const auto found = s.overrides.find(key);
    return found != s.overrides.end() ? found->second : nullptr;
}

} // namespace mhp3rd::settings
