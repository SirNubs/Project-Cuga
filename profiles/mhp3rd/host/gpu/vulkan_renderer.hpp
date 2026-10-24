#pragma once

#include "ge_state.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "input/touch_action.hpp"
#include "input/touch_controls.hpp"
#include "settings/settings.hpp"

union SDL_Event;
struct SDL_Window;
struct SDL_Gamepad;
struct ImDrawData;

namespace mhp3rd::gpu {

// PSP pad state gathered from the keyboard, the mouse's buttons and the gamepad.
struct PadState {
    std::uint32_t buttons{};
    std::uint8_t analog_x{0x80u};
    std::uint8_t analog_y{0x80u};
    // The HD release reads a second stick from the two SceCtrlData bytes after
    // Ly, which the PSP itself left reserved. 0x80 is its centre: the guest
    // skips its camera path entirely only when both bytes are exactly centred.
    std::uint8_t right_x{0x80u};
    std::uint8_t right_y{0x80u};
    // The host's fast-forward bind (kernel/fast_forward.hpp), never shown to
    // the game.
    bool fast_forward{};
};

// The mouse the input script's events come from. With scripted input on,
// only these reach the game, so a person moving the real pointer over the
// window does not disturb a scripted run.
inline constexpr std::uint32_t kScriptedMouse = 0xFFFFFF00u;

// How often a load running faster than real time shows a picture: about 30
// times a second, which a display never makes wait (set_fast_forward).
inline constexpr std::chrono::milliseconds kFastForwardPresentInterval{33};

// Relative mouse motion, in counts, while the pointer is captured for the game.
struct MouseMotion {
    float x{};
    float y{};
};

// What the free camera (camera/free_camera.hpp) is asked to do, read from the
// keyboard, the mouse wheel and the gamepad while Experimental > Free camera
// is on. Presses count once, from one take to the next; the rest is held.
struct FreeCameraControls {
    bool toggle{};     // F6, or Back (Select) + R3 on a gamepad
    bool pause{};      // P, or Start: the photo mode's pause
    bool reset{};      // R, or Y (north): back to where the game's camera is
    int speed_steps{}; // mouse wheel, + and -, D-pad up and down
    float right{};     // D and A, the left stick
    float forward{};   // W and S, the left stick
    float up{};        // E and Q, RB and LB
    float look_x{};    // the right stick past its dead zone, -1..1, inverted as the camera settings say
    float look_y{};
    bool fast{}; // Left Shift, RT
    bool slow{}; // Left Ctrl, LT
};

// The camera the game itself set, read back from the view matrix it uploads.
// Only filled while MHP3RD_TRACE_CAMERA or MHP3RD_FIND_CAMERA is on.
struct CameraReading {
    bool valid{};
    float yaw{};   // degrees, from the direction the camera looks along
    float pitch{}; // degrees
    float turn{};  // degrees of yaw since the previous traced frame
    std::array<float, 3> position{};
    // The matrix itself, in the layout the game holds it in: the GE's twelve
    // uploaded floats expanded to a 4x4, which is byte for byte the matrix the
    // game passed to the GE.
    std::array<float, 16> view{};
};

struct RendererConfig {
    std::string title{"Yakumo"};
};

// Vulkan backend for the GE. Draw calls are rendered into an offscreen target
// per guest framebuffer, which is blitted to the window once per guest frame.
// A target holds the game's 480x272 screen at a multiple of that size, or,
// under Fill, at the window's shape: then each PSP pixel is wider (or taller)
// than square, the game draws a view of that shape (camera/game_aspect.hpp),
// and the 2D interface is drawn at its own proportions.
class VulkanRenderer {
public:
    VulkanRenderer();
    ~VulkanRenderer();
    VulkanRenderer(const VulkanRenderer &) = delete;
    VulkanRenderer &operator=(const VulkanRenderer &) = delete;

    // Returns false and fills `error` when the window or device cannot be created.
    bool initialize(const RendererConfig &config, std::string &error);
    void shutdown();
    [[nodiscard]] bool available() const noexcept;

    // Pumps window events; returns false once the window has been closed.
    bool pump_events();
    [[nodiscard]] PadState pad() const noexcept;
    // Reads the keyboard, mouse buttons and gamepad again for pad() without
    // handling window events (issue #8): the game reads its pad each frame,
    // and taking the state then instead of at the last flip saves up to a
    // game frame of latency.
    void sample_pad();
    // The mouse's motion gathered by the pumps since the last call. Only
    // motion made while the pointer was captured for the game counts.
    [[nodiscard]] MouseMotion take_mouse_motion() noexcept;
    // The on-screen touch controls: shown once the screen is touched while the
    // game runs, hidden again by a gamepad, the keyboard or a real mouse.
    [[nodiscard]] bool touch_controls_visible() const noexcept;
    [[nodiscard]] const input::touch::Controls &touch_controls() const;
    // The action layout's controls (#174), which take the fingers instead
    // while settings choose that layout.
    [[nodiscard]] const input::touch::ActionControls &action_touch_controls() const;
    // A camera drag on the touch screen since the last take, as a fraction of
    // the screen's height.
    [[nodiscard]] MouseMotion take_touch_motion() noexcept;
    // The on-screen menu button was tapped since the last take.
    [[nodiscard]] bool take_touch_menu() noexcept;
    // The pointer is captured for the game: hidden, and its motion and
    // buttons go to the game. That is while the mouse setting is on, the game
    // has input, no interface screen is up and the window has focus.
    [[nodiscard]] bool mouse_captured() const noexcept;

    [[nodiscard]] bool quit_requested() const noexcept;

    void begin_frame();
    // Call before walking each display list. Guest memory cannot change while a
    // list is walked, so texture contents are hashed once per list, not per draw.
    void begin_display_list();
    // GPU vertex decode (MHP3RD_GPU_DECODE): whether the display list about
    // to run should hand transformed triangle draws over undecoded
    // (GeState::set_raw_vertices), and whether it should also decode them
    // for MHP3RD_CHECK_GPU_DECODE.
    [[nodiscard]] bool gpu_decode() const;
    [[nodiscard]] bool check_gpu_decode() const;
    void submit(const DrawCall &call, const GuestMemory &memory);
    // Writes the framebuffer shown a frame or two ago back to guest VRAM, in
    // the guest's pixel format at 480x272, so game code that copies a frame
    // out of VRAM with the CPU or DMA finds the picture instead of stale
    // bytes. Call once per frame before present(). MHP3RD_NO_FB_TEXTURES turns
    // it off along with sampling render targets as textures.
    void write_back_frame(GuestMemory &memory);
    // Before a GE block transfer reads guest memory: when `source` lies in a
    // framebuffer the renderer drew, finishes the work queued so far and
    // writes that framebuffer back to guest memory, so the copy gets the
    // picture. Waits for the GPU. MHP3RD_NO_FB_TEXTURES turns it off.
    void read_back_framebuffer(std::uint32_t source, GuestMemory &memory);
    // Ends the frame and shows the target the guest just flipped to. Draws go to
    // a separate offscreen target per guest framebuffer address, so only the
    // displayed one reaches the window. `moment` is the real time the flip's
    // emulated time stands for (Kernel::real_time_of). With frame
    // interpolation the frame is shown by the presents that follow, between
    // this flip and the next, which count themselves (perf::count_present);
    // returns whether the flip itself presented the frame.
    bool present(
        std::uint32_t display_address, std::optional<std::chrono::steady_clock::time_point> moment = std::nullopt);
    // Frame interpolation (Video > Frame rate; gpu/frame_pacing.hpp). The
    // kernel calls present_due() while the game's code runs, to make a
    // present that has fallen due, and present_until() while it waits for
    // real time, to make those due before `wake`, sleeping up to each.
    void present_due();
    void present_until(std::chrono::steady_clock::time_point wake);
    // Drops the presents scheduled, as the game pauses.
    void pause_interpolation();
    // While on, present() shows each frame at once, as at 30 frames a second:
    // the free camera's photo mode draws the game's last frame again and
    // again while the game stands still, with nothing in between to blend.
    void set_still(bool still);
    // A load running faster than real time (kernel/fast_loading.hpp) flips
    // several times per refresh of the display. While it does, a flip reaches
    // the window only if the one before it was shown at least
    // kFastForwardPresentInterval ago; the others are drawn and not shown, and
    // frame interpolation waits until it is over.
    void set_fast_forward(bool on);
    void set_frame_rate(settings::FrameRate rate);
    // On, the frame rate steps down by itself rather than slow the game.
    void set_frame_rate_auto(bool automatic);
    // The refresh rate of the window's display as SDL reports it, 0 when unknown.
    [[nodiscard]] float display_refresh() const noexcept;
    // The rate frames are presented at now: the setting's, or a slower one
    // the renderer stepped down to so that the game keeps its speed.
    [[nodiscard]] double frame_rate_now() const noexcept;
    // Shows a frame the game wrote to memory itself instead of drawing it
    // with the GE, as the movie player does: the next present of
    // `display_address` shows these `width` x `height` pixels (R, G, B, A in
    // memory order, rows `stride` pixels apart), scaled to the target. Call
    // it at most once per presented frame.
    void upload_frame(std::uint32_t display_address, const std::uint8_t *pixels, std::uint32_t width,
        std::uint32_t height, std::uint32_t stride);

    // Writes the last rendered frame as a BMP; returns false if it could not be
    // read back. Used for screenshots without touching the window system.
    bool capture_frame(const std::filesystem::path &path);
    // The last frame the game flipped to (or the photo mode drew again), as
    // the game's render target holds it: `width` x `height` pixels at the
    // size the game is drawn at, R G B A, top row first, with nothing the
    // port draws over the window. Waits for the GPU. False if there is none.
    bool read_frame(std::vector<std::uint8_t> &pixels, std::uint32_t &width, std::uint32_t &height);
    // Writes the next presented window image, with the interface over it, as
    // a BMP once it has been drawn.
    void capture_window(const std::filesystem::path &path);

    // Display settings, applied at once. The initial values come from
    // settings::current() in initialize(). A scale of 0 follows the window's
    // size in pixels, including every later change of it.
    void set_internal_scale(std::uint32_t scale);
    void set_window_scale(std::uint32_t scale);
    void set_fullscreen(bool fullscreen);
    void set_present_mode(settings::PresentMode mode);
    [[nodiscard]] bool supports_present_mode(settings::PresentMode mode) const;
    void set_aspect(settings::Aspect aspect);
    void set_sharp_screen(bool sharp);
    void set_sharp_textures(bool sharp);
    // Draws an installed HD texture pack's images instead of the game's own
    // textures (texture_pack.hpp). Takes effect from the next frame; off
    // draws exactly what no pack would.
    void set_texture_pack(bool enabled);
    // For the menu: "Off", "Not installed", or how many textures the pack has
    // and how many of them are on the GPU.
    [[nodiscard]] std::string texture_pack_status() const;
    // Opens the pack again from the next frame, e.g. after an import.
    void reload_texture_pack();
    // While held, no pack is open, so an import can move its folder; true
    // from texture_pack_held() once the pack has been closed.
    void hold_texture_pack(bool hold);
    [[nodiscard]] bool texture_pack_held() const;
    // The folder the pack is read from (texture_pack_import.hpp): the
    // installed one, textures/<disc id> in the data directory, or the one the
    // player uses in place or MHP3RD_TEXTURE_PACK names.
    [[nodiscard]] std::string texture_pack_folder() const;
    // textures/ in the data directory.
    [[nodiscard]] static std::filesystem::path textures_root();
    void set_perf_overlay(bool visible);

    // The shape the game's 3D view should have, width over height: the
    // target's under Fill, the PSP's 480/272 otherwise.
    [[nodiscard]] float game_aspect() const noexcept;
    // Where the game's picture is in the window: left, top, width and
    // height, each a fraction of the window's.
    [[nodiscard]] std::array<float, 4> game_picture() const noexcept;
    // The size the game is drawn at, in pixels.
    [[nodiscard]] std::array<std::uint32_t, 2> target_size() const noexcept;

    [[nodiscard]] SDL_Window *window() const noexcept;
    [[nodiscard]] std::string device_name() const;
    // The GPU and its driver as far as initialize() got before it failed
    // ("Mali-G57 MC2, driver r32p1, Vulkan 1.1.177"), for an error a player
    // can report from a screenshot; empty when no GPU was chosen yet.
    [[nodiscard]] std::string device_summary() const;
    // Why the game's picture may be missing or wrong (a failed start-up
    // self-test, pipelines the driver refused), for the interface to show
    // over the game; empty while nothing is known to be wrong.
    [[nodiscard]] std::string gpu_problem() const;
    // Whether GPU compatibility mode is on this run, and why, for the menu:
    // "On (reason)" or "Off".
    [[nodiscard]] std::string gpu_compat_status() const;
    // The pad the game reads, or null.
    [[nodiscard]] SDL_Gamepad *gamepad() const noexcept;

    // The port's own interface (host/ui). Every window event is offered to
    // the hook first; returning true keeps it from the game.
    void set_event_hook(std::function<bool(const SDL_Event &)> hook);
    // While off, the game reads a neutral pad. Turning it back on ignores the
    // buttons still held until they are released, so the button that closed
    // a menu does not reach the game.
    void set_game_input(bool enabled);
    // While on, the game reads a neutral pad and the keyboard, the mouse and
    // the gamepad fly the free camera instead. Turning it off ignores the
    // buttons still held until they are released, as set_game_input does.
    void set_free_camera(bool flying);
    // The free camera's controls since the last take; nothing unless the
    // free camera setting is on and the game has input.
    [[nodiscard]] FreeCameraControls take_free_camera_controls();
    // The screenshot bind was pressed since the last take (#187). Read in
    // play, in the free camera and in its photo mode, not while a menu has
    // the input.
    [[nodiscard]] bool take_screenshot_request() noexcept;
    // The frame step bind is held now (#187); only the photo mode acts on it.
    [[nodiscard]] bool frame_step_held() const noexcept;
    // A press of the Hide HUD bind since the last take (gpu/game_hud.hpp).
    [[nodiscard]] bool take_hide_hud_toggle() noexcept;
    // A tap of the Lock on bind since the last call (camera/lock_on.hpp).
    [[nodiscard]] bool take_lock_on_press() noexcept;
    // A window capture (capture_window) waits for the next present, so the
    // interface can leave out what does not belong in a picture.
    [[nodiscard]] bool window_capture_pending() const noexcept;
    // An interface screen is up (host/ui), even one without the game behind
    // it such as the setup: the pointer stays free for it.
    void set_pointer_free(bool free);
    // The input script (MHP3RD_INPUT_SCRIPT): keys it holds reach the game as
    // well as the interface, and with scripted input on, the pointer counts as
    // captured without the window having focus and without taking the real
    // pointer, so scripted mouse steps reach the game from the background.
    void set_scripted_key(int position, bool down);
    void set_scripted_input(bool scripted);
    void request_quit() noexcept;
    // While held, the window keeps showing the frame on screen when hold
    // began instead of the frames the game flips to. The game blanks its
    // screen while the PSP's own keyboard would cover it; the port's keyboard
    // is drawn over the held frame instead.
    void hold_frame(bool hold);

    // Sets up Dear ImGui's Vulkan backend on this window; the caller has
    // created the ImGui context and its SDL3 backend.
    bool initialize_ui(std::string &error);
    void shutdown_ui();
    // ImGui_ImplVulkan_NewFrame, before ImGui::NewFrame.
    void begin_ui_frame();
    // Draw data from ImGui::Render, drawn over the next presented image.
    void set_ui_draw_data(ImDrawData *draw_data);
    // Presents a frame outside the game's own flips: the last game frame when
    // there is one and `show_game` is set, a plain background otherwise, with
    // the interface over it. Used while the game is paused or not started.
    void present_ui(bool show_game);

    [[nodiscard]] std::uint64_t frames_presented() const noexcept;
    [[nodiscard]] std::uint64_t draws_submitted() const noexcept;
    [[nodiscard]] CameraReading camera() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mhp3rd::gpu
