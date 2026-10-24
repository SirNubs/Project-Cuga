#include "ui/layer.hpp"

#include "app_paths.hpp"

#include "ui/controllers_screen.hpp"
#include "ui/input_script.hpp"
#include "ui/widgets.hpp"

#include "gpu/vulkan_renderer.hpp"
#include "input/bindings.hpp"
#include "install/user_data.hpp"
#include "platform/utf8_path.hpp"
#include "settings/settings.hpp"

#include "backends/imgui_impl_sdl3.h"
#include "imgui.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace mhp3rd::ui {
namespace {

// How long an Esc waits for a gamepad press that would mark it as sent by
// Steam's controller layout rather than by a keyboard.
constexpr auto kEscapeWindow = std::chrono::milliseconds(100);

// Interface frames are presented with the game's present mode; without vsync
// they would spin, so they are held to about 120 per second.
constexpr auto kMinFrameTime = std::chrono::microseconds(8'333);

// Text faces with Latin and Cyrillic, then a Japanese face merged in for file
// names. The first one found is used; a release's own font in fonts/ is the
// last resort for the Japanese face.
const char *const kTextFonts[] = {
    "/System/Library/Fonts/SFNS.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/arial.ttf",
};
const char *const kJapaneseFonts[] = {
    "/System/Library/Fonts/ヒラギノ角ゴシック W4.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
    "/run/host/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
    "/run/host/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/run/host/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
    "C:/Windows/Fonts/meiryo.ttc",
    "C:/Windows/Fonts/msgothic.ttc",
};

bool exists(const char *path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(install::path_from_utf8(path), ec);
}

void load_fonts() {
    ImGuiIO &io = ImGui::GetIO();
    // Dear ImGui opens font files by UTF-8 name on every platform.
    const std::optional<std::string> ui_font = environment_utf8("MHP3RD_UI_FONT");
    const char *text_font = ui_font ? ui_font->c_str() : nullptr;
    if (text_font != nullptr && !exists(text_font)) {
        std::cout << "[ui] MHP3RD_UI_FONT " << text_font << " not found\n";
        text_font = nullptr;
    }
    for (const char *candidate : kTextFonts) {
        if (text_font != nullptr) break;
        if (exists(candidate)) text_font = candidate;
    }
    ImFont *font = text_font != nullptr ? io.Fonts->AddFontFromFileTTF(text_font) : nullptr;
#if defined(__ANDROID__)
    // Android's own faces are variable fonts; the Japanese font the app
    // carries has Latin too, and the symbols the menu uses (… ○ ×).
    if (font == nullptr)
        for (const std::filesystem::path &bundled : bundled_fonts()) {
            font = io.Fonts->AddFontFromFileTTF(install::path_to_utf8(bundled).c_str());
            if (font != nullptr) {
                std::cout << "[ui] text in " << install::path_to_utf8(bundled.filename()) << "\n";
                return;
            }
        }
#endif
    if (font == nullptr) {
        io.Fonts->AddFontDefaultVector();
        std::cout << "[ui] no system font found; using Dear ImGui's own\n";
        return;
    }
    std::vector<std::string> japanese(std::begin(kJapaneseFonts), std::end(kJapaneseFonts));
    for (const std::filesystem::path &bundled : bundled_fonts()) japanese.push_back(install::path_to_utf8(bundled));
    for (const std::string &candidate : japanese) {
        if (!exists(candidate.c_str())) continue;
        ImFontConfig merge;
        merge.MergeMode = true;
        io.Fonts->AddFontFromFileTTF(candidate.c_str(), 0.0f, &merge);
        break;
    }
}

// Any button or trigger of any gamepad held.
bool pad_input_held() {
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    bool held = false;
    for (int i = 0; ids != nullptr && i < count && !held; ++i) {
        SDL_Gamepad *pad = SDL_GetGamepadFromID(ids[i]);
        if (pad == nullptr) continue;
        for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button)
            held = held || SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(button));
        for (SDL_GamepadAxis axis : {SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER})
            held = held || SDL_GetGamepadAxis(pad, axis) > 8000;
    }
    SDL_free(ids);
    return held;
}

bool face_button_held() {
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    bool held = false;
    for (int i = 0; ids != nullptr && i < count && !held; ++i) {
        SDL_Gamepad *pad = SDL_GetGamepadFromID(ids[i]);
        if (pad == nullptr) continue;
        for (SDL_GamepadButton button : {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST,
                 SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_START})
            held = held || SDL_GetGamepadButton(pad, button);
    }
    SDL_free(ids);
    return held;
}

} // namespace

Layer &Layer::get() {
    static Layer layer;
    return layer;
}

bool Layer::attach(gpu::VulkanRenderer &renderer) {
    if (renderer_ != nullptr) return true;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    // Keep the focused row highlighted: on a gamepad there is no pointer.
    io.ConfigNavCursorVisibleAlways = true;
    io.ConfigNavEscapeClearFocusItem = false;
    if (!ImGui_ImplSDL3_InitForVulkan(renderer.window())) {
        std::cout << "[ui] ImGui_ImplSDL3_InitForVulkan failed; no menu\n";
        ImGui::DestroyContext();
        return false;
    }
    // Any connected pad drives the interface, not only the one the game reads.
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_AutoAll);
    load_fonts();
    std::string error;
    if (!renderer.initialize_ui(error)) {
        std::cout << "[ui] cannot draw the interface (" << error << "); no menu\n";
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    renderer.set_event_hook([this](const SDL_Event &event) { return handle_event(event); });
    renderer_ = &renderer;
    // While the game runs the pointer may be captured for it, hidden by SDL;
    // the overlays drawn then must not show it again.
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    if (renderer.gamepad() != nullptr) device_ = InputDevice::Gamepad;
    note_unknown_controllers();
    script::attach();
    return true;
}

void Layer::set_interactive(bool interactive) {
    if (interactive == interactive_) return;
    interactive_ = interactive;
    ImGuiIO &io = ImGui::GetIO();
    // Nothing typed while the game ran is replayed into a screen, and nothing
    // held when a screen closes stays held for the next one.
    io.ClearEventsQueue();
    io.ClearInputKeys();
    if (interactive)
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    else
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    // A screen needs the pointer; the renderer frees it before the next frame.
    if (renderer_ != nullptr) renderer_->set_pointer_free(interactive);
    menu_toggle_ = false;
    back_ = false;
    capturing_binding_ = false;
    capture_held_.clear();
    captured_binding_.reset();
    escape_pending_.reset();
    gamepad_armed_ = false;
    pad_blocked_ = false;
}

bool Layer::confirm_south() const {
    return settings::current().confirm_south;
}

void Layer::begin_binding_capture(Capture device) {
    capturing_binding_ = true;
    capture_device_ = device;
    capture_started_ = Clock::now();
    capture_held_.clear();
    capture_down_.clear();
    capture_triggers_[0] = capture_triggers_[1] = false;
    captured_binding_.reset();
    escape_pending_.reset();
    if (device == Capture::Pad) pad_quiet_ = true;
}

float Layer::capture_cancel_progress() const {
    if (!capturing_binding_ || capture_device_ != Capture::Pad || capture_held_.size() != 1u ||
        capture_held_.front() != pad_back_button() || capture_down_.empty())
        return 0.0f;
    const auto held = std::chrono::duration<float>(Clock::now() - capture_first_press_);
    return std::clamp(held / std::chrono::duration<float>(kHoldToCancel), 0.0f, 1.0f);
}

input::Binding Layer::pad_back_button() const {
    return input::pad(confirm_south() ? input::PadInput::East : input::PadInput::South);
}

int Layer::capture_seconds_left() const {
    const auto left = kPadCaptureTimeout - (Clock::now() - capture_started_);
    return std::max(0, static_cast<int>(std::chrono::ceil<std::chrono::seconds>(left).count()));
}

void Layer::finish_capture(bool cancelled) {
    input::Chord chord;
    if (!cancelled)
        for (std::size_t i = 0; i < capture_held_.size() && i < input::kChordInputs; ++i)
            chord.inputs[i] = capture_held_[i];
    capturing_binding_ = false;
    captured_binding_ = chord;
    capture_held_.clear();
    capture_down_.clear();
}

std::optional<input::Chord> Layer::take_captured_binding() {
    return std::exchange(captured_binding_, std::nullopt);
}

bool Layer::handle_event(const SDL_Event &event) {
    const Clock::time_point now = Clock::now();
    // Presses go to the binding being captured. Releases of what was held
    // before still reach ImGui, which saw the press that started the capture.
    if (capturing_binding_ && interactive_) {
        const auto press = [&](input::Binding binding) {
            if (std::find(capture_held_.begin(), capture_held_.end(), binding) != capture_held_.end()) return;
            if (capture_held_.size() == input::kChordInputs) return;
            if (capture_held_.empty()) capture_first_press_ = now;
            capture_held_.push_back(binding);
            capture_down_.push_back(binding);
        };
        // The chord is everything pressed until the last of it is let go.
        const auto release = [&](input::Binding binding) {
            const auto down = std::find(capture_down_.begin(), capture_down_.end(), binding);
            if (down == capture_down_.end()) return false;
            capture_down_.erase(down);
            if (capture_down_.empty()) finish_capture(false);
            return true;
        };
        const bool escape = event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
            (event.key.key == SDLK_ESCAPE
#if defined(__ANDROID__)
                || event.key.key == SDLK_AC_BACK
#endif
            );
        if (escape) {
            finish_capture(true);
            return true;
        }
        // A finger cancels: a touch screen has no key or button to bind.
        const bool touch = event.type == SDL_EVENT_FINGER_DOWN ||
            (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.which == SDL_TOUCH_MOUSEID);
        if (touch) {
            finish_capture(true);
            swallow_touch_ = true;
            return true;
        }
        if (capture_device_ == Capture::Keys) {
            switch (event.type) {
            case SDL_EVENT_KEY_DOWN:
                if (!event.key.repeat) press(input::key(static_cast<std::uint16_t>(event.key.scancode)));
                return true;
            case SDL_EVENT_KEY_UP:
                if (release(input::key(static_cast<std::uint16_t>(event.key.scancode)))) return true;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button >= 1u && event.button.button <= 5u) {
                    press(input::mouse_button(event.button.button));
                    return true;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (release(input::mouse_button(event.button.button))) return true;
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                device_ = InputDevice::Gamepad;
                last_pad_button_ = now;
                finish_capture(true);
                return true;
            default:
                break;
            }
        } else {
            switch (event.type) {
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                device_ = InputDevice::Gamepad;
                last_pad_button_ = now;
                press(input::pad(static_cast<input::PadInput>(event.gbutton.button)));
                return true;
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                release(input::pad(static_cast<input::PadInput>(event.gbutton.button)));
                return true;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
                const bool left = event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
                if (!left && event.gaxis.axis != SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) return true;
                const input::Binding trigger =
                    input::pad(left ? input::PadInput::LeftTrigger : input::PadInput::RightTrigger);
                const bool down = static_cast<float>(event.gaxis.value) / 32767.0f > settings::current().trigger;
                bool &was = capture_triggers_[left ? 0 : 1];
                if (down && !was) press(trigger);
                if (!down && was) release(trigger);
                was = down;
                return true;
            }
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                return true;
            default:
                break;
            }
        }
    }
    if (swallow_touch_) {
        const bool from_touch =
            ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
                event.button.which == SDL_TOUCH_MOUSEID) ||
            (event.type == SDL_EVENT_MOUSE_MOTION && event.motion.which == SDL_TOUCH_MOUSEID);
        const bool lifted = event.type == SDL_EVENT_FINGER_UP ||
            (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.which == SDL_TOUCH_MOUSEID);
        if (lifted) swallow_touch_ = false;
        if (from_touch || event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_MOTION) return true;
    }
    switch (event.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        window_closed_ = true;
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        // Android's Back is Esc: it opens and closes the menu.
        if (event.key.key == SDLK_ESCAPE
#if defined(__ANDROID__)
            || event.key.key == SDLK_AC_BACK
#endif
        ) {
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) escape_pending_ = now;
            return true;
        }
        if (event.type == SDL_EVENT_KEY_DOWN) device_ = InputDevice::Keyboard;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_WHEEL:
        device_ = InputDevice::Keyboard;
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
        device_ = InputDevice::Gamepad;
        last_pad_button_ = now;
        // A Steam Esc that came first is dropped here.
        if (escape_pending_ && now - *escape_pending_ < kEscapeWindow) escape_pending_.reset();
        const auto button = static_cast<SDL_GamepadButton>(event.gbutton.button);
        if (button == SDL_GAMEPAD_BUTTON_LEFT_STICK || button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) {
            SDL_Gamepad *pad = SDL_GetGamepadFromID(event.gbutton.which);
            const SDL_GamepadButton other = button == SDL_GAMEPAD_BUTTON_LEFT_STICK ? SDL_GAMEPAD_BUTTON_RIGHT_STICK
                                                                                    : SDL_GAMEPAD_BUTTON_LEFT_STICK;
            if (pad != nullptr && SDL_GetGamepadButton(pad, other) && !pad_blocked_) menu_toggle_ = true;
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (std::abs(static_cast<int>(event.gaxis.value)) > 16000) device_ = InputDevice::Gamepad;
        break;
    case SDL_EVENT_JOYSTICK_ADDED:
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
        // A controller nothing reads yet says so (#147).
        note_unknown_controller(event);
        return false;
    case SDL_EVENT_GAMEPAD_ADDED:
    case SDL_EVENT_GAMEPAD_REMOVED:
        // ImGui refreshes its list of pads only when it sees one of these. A
        // pad that connects while the game runs (one woken over Bluetooth)
        // would otherwise never reach the menu, though the game reads it.
        ImGui_ImplSDL3_ProcessEvent(&event);
        return false;
    case SDL_EVENT_DROP_FILE:
#if defined(MHP3RD_ANDROID_APP)
        // Android has no dropping: this is a document another app asked
        // Yakumo to open, and SDL passes only the path part of its content://
        // URI, which names no file. Nothing can be read from it.
        if (event.drop.data != nullptr)
            std::cout << "[ui] ignored a document opened with Yakumo: " << event.drop.data << "\n";
#else
        if (event.drop.data != nullptr) dropped_ = install::path_from_utf8(event.drop.data);
#endif
        return true;
    default:
        break;
    }
    if (!interactive_) return false;
    ImGui_ImplSDL3_ProcessEvent(&event);
    return true;
}

void Layer::resolve_escape() {
    if (!escape_pending_) return;
    const Clock::time_point now = Clock::now();
    if (now - *escape_pending_ < kEscapeWindow) return;
    const bool from_pad = *escape_pending_ - last_pad_button_ < kEscapeWindow;
    escape_pending_.reset();
    if (from_pad) return;
    if (interactive_)
        back_ = true;
    else
        menu_toggle_ = true;
}

bool Layer::take_menu_toggle() {
    resolve_escape();
    return std::exchange(menu_toggle_, false);
}

bool Layer::take_back() {
    resolve_escape();
    return std::exchange(back_, false);
}

std::optional<std::filesystem::path> Layer::take_dropped_file() {
    return std::exchange(dropped_, std::nullopt);
}

void Layer::apply_theme() {
    const ImGuiIO &io = ImGui::GetIO();
    // About 27 px on a Steam Deck's 800 lines, 18 px in the default 544-line
    // window, growing with larger windows.
    const float size = std::clamp(std::round(io.DisplaySize.y * 0.034f), 16.0f, 72.0f);
    if (size == font_size_) return;
    font_size_ = size;
    ImGui::GetStyle() = make_style(scale(), font_size_);
}

void Layer::begin_frame() {
    renderer_->begin_ui_frame();
    // Blocked, the interface reads no gamepad at all, while the keyboard,
    // the mouse and touches still work. Afterwards the pads count again once
    // nothing on them is held.
    if (pad_blocked_ != pads_detached_) {
        pads_detached_ = pad_blocked_;
        if (pad_blocked_) {
            ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
        } else {
            ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_AutoAll);
            pad_quiet_ = true;
        }
        ImGui::GetIO().ClearInputKeys();
    }
    ImGui_ImplSDL3_NewFrame();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigNavSwapGamepadButtons = !confirm_south();
    if (capturing_binding_ && capture_device_ == Capture::Pad && capture_held_.empty() &&
        Clock::now() - capture_started_ > kPadCaptureTimeout)
        finish_capture(true);
    if (capture_cancel_progress() >= 1.0f) finish_capture(true);
    // While a gamepad binding is captured, and until the pad is let go of
    // after it, the interface does not see the pad at all.
    if (pad_quiet_ && !capturing_binding_ && !pad_input_held()) pad_quiet_ = false;
    if (pad_quiet_) {
        io.ClearEventsQueue();
        io.ClearInputKeys();
        gamepad_armed_ = false;
    }
    if (!gamepad_armed_) gamepad_armed_ = !pad_quiet_ && !face_button_held();
    if (gamepad_armed_)
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    else
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    apply_theme();
    ImGui::NewFrame();
    description_.clear();
}

void Layer::end_frame() {
    ImGui::Render();
    renderer_->set_ui_draw_data(ImGui::GetDrawData());
}

bool Layer::run(const std::function<bool()> &frame, bool show_game) {
    for (;;) {
        const Clock::time_point start = Clock::now();
        script::tick();
        if (!renderer_->pump_events()) window_closed_ = true;
        if (window_closed_) return false;
        begin_frame();
        const bool keep_going = frame();
        end_frame();
        renderer_->present_ui(show_game);
        if (!keep_going) return true;
        const Clock::duration spent = Clock::now() - start;
        if (spent < kMinFrameTime) std::this_thread::sleep_for(kMinFrameTime - spent);
    }
}

} // namespace mhp3rd::ui
