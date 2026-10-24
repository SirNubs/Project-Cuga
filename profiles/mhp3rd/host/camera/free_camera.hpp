#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

namespace psprecomp {
class Runtime;
class GuestMemory;
}
namespace mhp3rd::gpu {
struct DrawCall;
}

// The free camera (issue #162): the view detached from the game's camera and
// flown about the scene, for pictures, for looking at models and levels, and
// for debugging the renderer. Experimental, off by default.
//
// NPJB-40001, as traced with MHP3RD_TRACE_VIEWS: every transformed draw of
// the village and of a quest area is made with one GE view matrix, and the
// game keeps that matrix in its camera object (the pointer at 0x08A2F958) at
// +0xF50, with a copy at +0xF90. The GE keeps 24 bits of each float, so the
// uploaded matrix is the kept one with the low 8 bits of every entry cleared.
//
// The free camera changes nothing the game can see. Game logic, the camera
// object, culling and level of detail run as before; only the draws whose
// view is the game's own get the free camera's view instead, on their way to
// the renderer (GeState::set_view_hook). Leaving hands every draw its own
// view again, so the game's camera is back exactly as it was.
//
// What the game culls against its own camera it never draws, so it is not
// in the picture from anywhere else either: see the profile README.
namespace mhp3rd::camera {

using Matrix = std::array<float, 16>; // column major, as the GE's matrices are expanded

// Where the camera is and where it looks. Yaw 0 looks along +z, and yaw grows
// towards +x; pitch looks up when positive. Degrees. No roll.
struct FreePose {
    std::array<float, 3> eye{};
    float yaw{};
    float pitch{};
};

// The pose of a GE view matrix (world to eye, looking along eye -z), or
// nothing when its rotation is not one: a matrix with a scale, a mirror or
// nothing in it. Roll is dropped.
[[nodiscard]] std::optional<FreePose> pose_of_view(const Matrix &view);
// The view matrix of a pose.
[[nodiscard]] Matrix view_of_pose(const FreePose &pose);

// What to do in one step, from any device.
struct FlyInput {
    float right{};   // -1..1: strafe
    float forward{}; // -1..1: along where the camera looks, up and down included
    float up{};      // -1..1: straight up or down
    // Degrees to turn: positive yaw turns right and positive pitch looks down,
    // as camera_input.hpp counts them.
    float yaw_degrees{};
    float pitch_degrees{};
};
inline constexpr float kFreePitchLimit = 89.0f;
// The pose after `seconds` of `input` at `units_per_second`.
[[nodiscard]] FreePose fly(FreePose pose, const FlyInput &input, float seconds, float units_per_second);

// Whether `uploaded` (a view matrix as the GE got it) is `kept` (the game's
// full-precision matrix) after the GE's truncation to 24 bits.
[[nodiscard]] bool same_uploaded(const Matrix &uploaded, const Matrix &kept);

// The game's own view, from its camera object, or nothing where there is no
// camera object that looks like one.
[[nodiscard]] std::optional<Matrix> game_view(const psprecomp::GuestMemory &memory);

// --- The running free camera ------------------------------------------------

struct FreeCameraStatus {
    bool active{}; // flying
    bool paused{}; // flying in the photo mode, the game stood still
    float speed{}; // units a second
    // Draws given the free camera's view and draws left as the game made
    // them during the last game frame, for the indicator and the trace.
    std::uint32_t moved_draws{};
    std::uint32_t other_draws{};
};
[[nodiscard]] FreeCameraStatus free_camera_status();
[[nodiscard]] bool free_camera_active();

// One game frame's requests, already turned into this module's terms.
struct FreeCameraRequest {
    bool toggle{};
    bool pause{};
    bool reset{};
    int speed_steps{};
    bool fast{};
    bool slow{};
    FlyInput input; // look already in degrees; movement as fractions
};

// Once per game flip, and once per shown frame while the photo mode holds
// the game: takes the request and moves the camera by `seconds` of it.
// Returns false while the game's view is not there to start from.
void free_camera_update(psprecomp::Runtime &runtime, const FreeCameraRequest &request, float seconds);
// Leaves the free camera, when the setting goes off or the game ends.
void free_camera_leave();

// The hook every display list's transformed draws pass through (GeState::
// set_view_hook): while flying, it gives the game's own view the free
// camera's; with MHP3RD_TRACE_VIEWS it counts the views. Empty otherwise,
// which is always the case with the setting off.
[[nodiscard]] std::function<void(gpu::DrawCall &)> free_camera_view_hook(const psprecomp::GuestMemory &memory);

// Once per game flip, after the frame was presented: MHP3RD_TRACE_VIEWS.
void free_camera_frame_end(psprecomp::Runtime &runtime);

// --- Frame step in the photo mode (#187) -------------------------------------

// When the frame step bind makes the game run on by one frame: once when it
// goes down, and while it is held, again after kFrameStepDelay and then
// every kFrameStepRepeat, about ten frames a second, a third of the game's
// own pace.
inline constexpr std::chrono::milliseconds kFrameStepDelay{400};
inline constexpr std::chrono::milliseconds kFrameStepRepeat{100};
class FrameStepRepeat {
public:
    using Clock = std::chrono::steady_clock;
    // Whether to step now, with the bind `held` at `now`.
    [[nodiscard]] bool update(bool held, Clock::time_point now);
    // Forgets a hold, as the photo mode ends.
    void reset() { held_ = false; }

private:
    bool held_{};
    Clock::time_point next_{};
};

} // namespace mhp3rd::camera
