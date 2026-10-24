#pragma once

#include "input/bindings.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

union SDL_Event;
struct ImFont;

namespace mhp3rd::gpu {
class VulkanRenderer;
}

// Shared machinery of the interface screens: the Dear ImGui context on the
// renderer's window, the theme and fonts scaled to the window, and input.
//
// Input. While a screen runs (the menu, the setup) it is "interactive": window
// events go to ImGui and none reach the game. While the game runs, events are
// only watched for the menu buttons.
//
// Esc is special. Steam's desktop controller layout on a Steam Deck sends Esc
// together with the B button, and B is a button the game and the menu use. An
// Esc that arrives within a moment of a gamepad button press is therefore
// dropped; a real key press is acted on a few frames late, which nobody notices.
namespace mhp3rd::ui {

enum class InputDevice { Keyboard, Gamepad };

class Layer {
public:
    static Layer &get();

    bool attach(gpu::VulkanRenderer &renderer);
    [[nodiscard]] bool attached() const noexcept { return renderer_ != nullptr; }
    [[nodiscard]] gpu::VulkanRenderer &renderer() noexcept { return *renderer_; }

    void set_interactive(bool interactive);

    // Runs interface frames until `frame` returns false: pumps events, starts
    // an ImGui frame, calls `frame` to build it and presents it over the last
    // game frame (show_game) or a plain background. False if the window was
    // closed meanwhile.
    bool run(const std::function<bool()> &frame, bool show_game);

    // One frame drawn over the running game, outside run().
    void begin_frame();
    void end_frame();

    // Requests from the player, each reported once.
    bool take_menu_toggle(); // L3+R3; Esc while the game runs
    bool take_back();        // Esc while a screen runs
    std::optional<std::filesystem::path> take_dropped_file();
    [[nodiscard]] bool window_closed() const noexcept { return window_closed_; }

    // Binding a control (the menu's bindings): what is pressed next is kept
    // for the caller instead of reaching the interface: one input, or up to
    // input::kChordInputs held together in any order (a chord), recorded
    // until the last of them is released. For Keys, keys and mouse buttons count, and Esc, a gamepad
    // button or a touch cancels; for Pad, gamepad buttons and triggers
    // count, and Esc, a touch, holding the menu's back button for
    // kHoldToCancel, or kPadCaptureTimeout without a press cancels. A short
    // press of the back button binds it like any other.
    enum class Capture { Keys, Pad };
    static constexpr auto kPadCaptureTimeout = std::chrono::seconds(6);
    static constexpr auto kHoldToCancel = std::chrono::milliseconds(1000);
    void begin_binding_capture(Capture device = Capture::Keys);
    [[nodiscard]] bool capturing_binding() const noexcept { return capturing_binding_; }
    [[nodiscard]] Capture capture_device() const noexcept { return capture_device_; }
    // The inputs pressed so far while capturing, in the order pressed.
    [[nodiscard]] const std::vector<input::Binding> &capture_held() const noexcept { return capture_held_; }
    // Seconds left before a gamepad capture gives up.
    [[nodiscard]] int capture_seconds_left() const;
    // 0 to 1: how long the back button has been held, alone, towards
    // cancelling a gamepad capture.
    [[nodiscard]] float capture_cancel_progress() const;
    // The gamepad button that backs out of the menu, as the pad has it.
    [[nodiscard]] input::Binding pad_back_button() const;
    // Once capture has ended: the chord pressed, or an empty one if cancelled.
    std::optional<input::Chord> take_captured_binding();

    // False while a face button held since the screen opened is still down;
    // gamepad presses count only once it is released.
    [[nodiscard]] bool gamepad_armed() const noexcept { return gamepad_armed_; }

    // While blocked, no gamepad reaches the interface, and after it only
    // once nothing on the pads is held: the controller setup reads the pad
    // itself (#147).
    void set_pad_blocked(bool blocked) noexcept { pad_blocked_ = blocked; }
    // What the player last used, for the button hints.
    [[nodiscard]] InputDevice input_device() const noexcept { return device_; }
    // Whether confirm is the south face button (the pad setting); the menu
    // follows the game's convention.
    [[nodiscard]] bool confirm_south() const;

    // Text size in pixels for the current window, and the matching factor
    // for spacing (1 at 20 px).
    [[nodiscard]] float font_size() const noexcept { return font_size_; }
    [[nodiscard]] float scale() const noexcept { return font_size_ / 20.0f; }

    // Explanation of the focused row, shown above the button hints.
    void set_description(const std::string &text) { description_ = text; }
    [[nodiscard]] const std::string &description() const noexcept { return description_; }

private:
    bool handle_event(const SDL_Event &event);
    void resolve_escape();
    void apply_theme();

    gpu::VulkanRenderer *renderer_{};
    bool interactive_{};
    bool window_closed_{};
    InputDevice device_{InputDevice::Keyboard};
    float font_size_{};
    std::string description_;

    using Clock = std::chrono::steady_clock;
    std::optional<Clock::time_point> escape_pending_;
    Clock::time_point last_pad_button_{};
    // Gamepad navigation waits until no face button is held, so a button held
    // while a screen opens does not activate its first row.
    bool gamepad_armed_{};
    bool menu_toggle_{};
    bool back_{};
    bool capturing_binding_{};
    Capture capture_device_{Capture::Keys};
    Clock::time_point capture_started_{};
    Clock::time_point capture_first_press_{};
    // A touch cancelled the capture: the pointer events of that finger reach
    // nothing until it lifts, so the tap does not also press a row.
    bool swallow_touch_{};
    std::vector<input::Binding> capture_held_;
    std::vector<input::Binding> capture_down_; // of those, the ones still down
    bool capture_triggers_[2]{};
    // After a gamepad capture, the pad reaches the interface again only once
    // nothing on it is held, so the chord just bound does nothing there.
    bool pad_quiet_{};
    bool pad_blocked_{};
    bool pads_detached_{};
    std::optional<input::Chord> captured_binding_;
    void finish_capture(bool cancelled);
    std::optional<std::filesystem::path> dropped_;
};

} // namespace mhp3rd::ui
