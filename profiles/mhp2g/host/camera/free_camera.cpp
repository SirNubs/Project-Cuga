#include "camera/free_camera.hpp"

#include "gpu/ge_state.hpp"
#include "settings/settings.hpp"

#include "psprecomp/guest_memory.hpp"
#include "psprecomp/runtime.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <set>
#include <vector>

namespace mhp2g::camera {
namespace {

constexpr float kDegrees = 57.29577951308232f;
constexpr float kRadians = 1.0f / kDegrees;

// NPJB-40001: the camera object, as game_aspect.cpp finds it, and the view
// matrix it keeps (MHP2G_TRACE_VIEWS names both places it holds it).
constexpr std::uint32_t kCameraPointer = 0x08A2F958u;
constexpr std::uint32_t kCameraBytes = 0x11D0u;
constexpr std::uint32_t kNear = 0x00u;
constexpr std::uint32_t kFar = 0x04u;
constexpr std::uint32_t kAspect = 0x08u;
constexpr std::uint32_t kFieldOfView = 0x0Cu;
constexpr std::uint32_t kViewMatrix = 0xF50u;

// Each speed step, from the wheel, + and - or the D-pad.
constexpr float kSpeedStep = 1.25f;
constexpr float kFastFactor = 4.0f;
constexpr float kSlowFactor = 0.25f;

using Vector = std::array<float, 3>;

float dot(const Vector &a, const Vector &b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vector cross(const Vector &a, const Vector &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

// The direction a pose looks in.
Vector forward_of(float yaw, float pitch) {
    const float y = yaw * kRadians;
    const float p = pitch * kRadians;
    return {std::cos(p) * std::sin(y), std::sin(p), std::cos(p) * std::cos(y)};
}

// The level direction to the camera's right: -z x forward, kept level.
Vector right_of(float yaw) {
    const float y = yaw * kRadians;
    return {-std::cos(y), 0.0f, std::sin(y)};
}

float load_float(const psprecomp::GuestMemory &memory, std::uint32_t address) {
    return std::bit_cast<float>(memory.load32(address));
}

// --- MHP2G_TRACE_VIEWS -----------------------------------------------------

struct ViewStats {
    Matrix view{};
    Matrix projection{};
    std::uint32_t draws{};
    std::uint32_t vertices{};
    std::uint32_t depth_writes{};
    std::set<std::uint32_t> targets;
};

std::uint64_t trace_every() {
    static const std::uint64_t value = [] {
        const char *text = std::getenv("MHP2G_TRACE_VIEWS");
        return text != nullptr ? std::max<std::uint64_t>(1u, std::strtoull(text, nullptr, 10)) : 0u;
    }();
    return value;
}

// --- The running camera -----------------------------------------------------

struct State {
    bool active{};
    bool paused{};
    FreePose pose;
    Matrix view{};
    float speed{};
    // This game frame's draws, and the last whole frame's.
    std::uint32_t moved{};
    std::uint32_t other{};
    std::uint32_t last_moved{};
    std::uint32_t last_other{};
    std::uint64_t frame{};
    std::vector<ViewStats> views; // MHP2G_TRACE_VIEWS
};

State &state() {
    static State value;
    return value;
}

void observe(State &s, const gpu::DrawCall &call) {
    auto found = std::find_if(s.views.begin(), s.views.end(),
        [&](const ViewStats &v) { return v.view == call.view && v.projection == call.projection; });
    if (found == s.views.end()) {
        s.views.push_back({call.view, call.projection});
        found = s.views.end() - 1;
    }
    ++found->draws;
    found->vertices += call.raw_vertices != nullptr ? call.raw_count : static_cast<std::uint32_t>(call.vertices.size());
    if (call.depth.write_enabled) ++found->depth_writes;
    found->targets.insert(call.target.color_address);
}

// Every place in the camera object a matrix with this view's twelve entries
// lies at, as the GE got them.
std::vector<std::uint32_t> find_in_camera(const psprecomp::GuestMemory &memory, const Matrix &view) {
    std::vector<std::uint32_t> found;
    const std::uint32_t camera = memory.load32(kCameraPointer);
    if (camera == 0u || !memory.contains(camera, kCameraBytes)) return found;
    for (std::uint32_t offset = 0; offset + 64u <= kCameraBytes; offset += 4u) {
        Matrix kept{};
        for (std::uint32_t i = 0; i < 16u; ++i) kept[i] = load_float(memory, camera + offset + i * 4u);
        if (same_uploaded(view, kept)) found.push_back(offset);
    }
    return found;
}

void print_views(const psprecomp::GuestMemory &memory, const State &s) {
    const std::ios::fmtflags flags = std::cout.flags();
    const std::streamsize precision = std::cout.precision();
    std::cout << "[views] frame " << s.frame << ": " << s.views.size() << " views, camera object 0x" << std::hex
              << memory.load32(kCameraPointer) << std::dec;
    if (s.active) std::cout << ", free camera moved " << s.moved << " draws and left " << s.other;
    std::cout << "\n" << std::fixed << std::setprecision(4);
    for (const ViewStats &v : s.views) {
        const auto pose = pose_of_view(v.view);
        std::cout << "[views]   draws=" << v.draws << " verts=" << v.vertices << " zwrite=" << v.depth_writes;
        if (pose)
            std::cout << " eye=(" << pose->eye[0] << "," << pose->eye[1] << "," << pose->eye[2] << ") yaw=" << pose->yaw
                      << " pitch=" << pose->pitch;
        else
            std::cout << " not a rotation";
        const Matrix &m = v.view;
        std::cout << " rows=[" << m[0] << " " << m[4] << " " << m[8] << " | " << m[1] << " " << m[5] << " " << m[9]
                  << " | " << m[2] << " " << m[6] << " " << m[10] << "] t=(" << m[12] << "," << m[13] << "," << m[14]
                  << ")";
        const Matrix &p = v.projection;
        std::cout << " proj=(" << p[0] << "," << p[5] << "," << p[10] << "," << p[11] << "," << p[14] << "," << p[15]
                  << ") targets=" << std::hex;
        for (std::uint32_t target : v.targets) std::cout << " 0x" << target;
        std::cout << std::dec;
        const auto places = find_in_camera(memory, v.view);
        if (!places.empty()) {
            std::cout << " in-camera" << std::hex;
            for (std::uint32_t place : places) std::cout << " +0x" << place;
            std::cout << std::dec;
        }
        std::cout << "\n";
    }
    std::cout.flags(flags);
    std::cout.precision(precision);
}

void say_pose(const char *what, const FreePose &pose, float speed) {
    const std::ios::fmtflags flags = std::cout.flags();
    const std::streamsize precision = std::cout.precision();
    std::cout << std::fixed << std::setprecision(1) << "[freecam] " << what << " at (" << pose.eye[0] << ", "
              << pose.eye[1] << ", " << pose.eye[2] << "), yaw " << pose.yaw << ", pitch " << pose.pitch << ", "
              << speed << " units/s" << std::endl;
    std::cout.flags(flags);
    std::cout.precision(precision);
}

// The camera object, when the pointer leads to one that looks like it.
std::uint32_t camera_address(const psprecomp::GuestMemory &memory) {
    const std::uint32_t camera = memory.load32(kCameraPointer);
    if (camera == 0u || !memory.contains(camera, kCameraBytes)) return 0u;
    const float near_plane = load_float(memory, camera + kNear);
    const float far_plane = load_float(memory, camera + kFar);
    const float aspect = load_float(memory, camera + kAspect);
    const float fov = load_float(memory, camera + kFieldOfView);
    const bool plausible = std::isfinite(near_plane) && std::isfinite(far_plane) && near_plane > 0.0f &&
        far_plane > near_plane && aspect > 0.25f && aspect < 8.0f && fov > 0.01f && fov < 3.1f;
    return plausible ? camera : 0u;
}

bool start_from_game(State &s, const psprecomp::GuestMemory &memory) {
    const std::optional<Matrix> view = game_view(memory);
    const std::optional<FreePose> pose = view ? pose_of_view(*view) : std::nullopt;
    if (!pose) return false;
    s.pose = *pose;
    s.view = view_of_pose(s.pose);
    return true;
}

} // namespace

std::optional<FreePose> pose_of_view(const Matrix &view) {
    const Vector r0{view[0], view[4], view[8]};
    const Vector r1{view[1], view[5], view[9]};
    const Vector r2{view[2], view[6], view[10]};
    // The GE's 24-bit floats keep about five digits.
    constexpr float kSlack = 1e-3f;
    for (const Vector *row : {&r0, &r1, &r2})
        if (std::fabs(dot(*row, *row) - 1.0f) > kSlack) return std::nullopt;
    if (std::fabs(dot(r0, r1)) > kSlack || std::fabs(dot(r1, r2)) > kSlack || std::fabs(dot(r0, r2)) > kSlack)
        return std::nullopt;
    if (dot(cross(r0, r1), r2) < 0.0f) return std::nullopt; // a mirror
    const Vector forward{-r2[0], -r2[1], -r2[2]};
    FreePose pose;
    pose.yaw = std::atan2(forward[0], forward[2]) * kDegrees;
    pose.pitch =
        std::clamp(std::asin(std::clamp(forward[1], -1.0f, 1.0f)) * kDegrees, -kFreePitchLimit, kFreePitchLimit);
    const float t0 = view[12], t1 = view[13], t2 = view[14];
    for (std::size_t axis = 0; axis < 3u; ++axis) pose.eye[axis] = -(r0[axis] * t0 + r1[axis] * t1 + r2[axis] * t2);
    return pose;
}

Matrix view_of_pose(const FreePose &pose) {
    const Vector forward = forward_of(pose.yaw, pose.pitch);
    const Vector back{-forward[0], -forward[1], -forward[2]};
    const Vector right = right_of(pose.yaw);
    const Vector up = cross(back, right);
    Matrix view{};
    for (std::size_t axis = 0; axis < 3u; ++axis) {
        view[axis * 4u + 0u] = right[axis];
        view[axis * 4u + 1u] = up[axis];
        view[axis * 4u + 2u] = back[axis];
    }
    view[12] = -dot(right, pose.eye);
    view[13] = -dot(up, pose.eye);
    view[14] = -dot(back, pose.eye);
    view[15] = 1.0f;
    return view;
}

FreePose fly(FreePose pose, const FlyInput &input, float seconds, float units_per_second) {
    pose.yaw -= input.yaw_degrees;
    pose.yaw = std::remainder(pose.yaw, 360.0f);
    pose.pitch = std::clamp(pose.pitch - input.pitch_degrees, -kFreePitchLimit, kFreePitchLimit);
    const Vector forward = forward_of(pose.yaw, pose.pitch);
    const Vector right = right_of(pose.yaw);
    // A diagonal is no faster than a straight line.
    float right_amount = std::clamp(input.right, -1.0f, 1.0f);
    float forward_amount = std::clamp(input.forward, -1.0f, 1.0f);
    const float level = std::sqrt(right_amount * right_amount + forward_amount * forward_amount);
    if (level > 1.0f) {
        right_amount /= level;
        forward_amount /= level;
    }
    const float up_amount = std::clamp(input.up, -1.0f, 1.0f);
    const float distance = units_per_second * std::max(seconds, 0.0f);
    for (std::size_t axis = 0; axis < 3u; ++axis)
        pose.eye[axis] += (right[axis] * right_amount + forward[axis] * forward_amount) * distance;
    pose.eye[1] += up_amount * distance;
    return pose;
}

bool same_uploaded(const Matrix &uploaded, const Matrix &kept) {
    for (std::size_t i = 0; i < 16u; ++i) {
        if ((i & 3u) == 3u) continue; // the GE uploads twelve entries
        const std::uint32_t a = std::bit_cast<std::uint32_t>(uploaded[i]) & 0xFFFFFF00u;
        const std::uint32_t b = std::bit_cast<std::uint32_t>(kept[i]) & 0xFFFFFF00u;
        if (a != b) return false;
    }
    return true;
}

std::optional<Matrix> game_view(const psprecomp::GuestMemory &memory) {
    const std::uint32_t camera = camera_address(memory);
    if (camera == 0u) return std::nullopt;
    Matrix view{};
    for (std::uint32_t i = 0; i < 16u; ++i) view[i] = load_float(memory, camera + kViewMatrix + i * 4u);
    if (!pose_of_view(view)) return std::nullopt;
    return view;
}

FreeCameraStatus free_camera_status() {
    const State &s = state();
    return {s.active, s.paused, s.speed, s.last_moved, s.last_other};
}

bool free_camera_active() {
    return state().active;
}

void free_camera_leave() {
    State &s = state();
    if (!s.active) return;
    s.active = false;
    s.paused = false;
    std::cout << "[freecam] off: the game's camera again" << std::endl;
    // The speed chosen while flying is the one to start with next time.
    settings::Settings &player = settings::current();
    if (settings::overridden_by("experimental.free_camera_speed") == nullptr && player.free_camera_speed != s.speed) {
        player.free_camera_speed = s.speed;
        settings::save();
    }
}

void free_camera_update(psprecomp::Runtime &runtime, const FreeCameraRequest &request, float seconds) {
    State &s = state();
    const psprecomp::GuestMemory &memory = runtime.memory();
    if (!settings::current().free_camera) {
        free_camera_leave();
        return;
    }
    if (request.toggle) {
        if (s.active) {
            free_camera_leave();
            return;
        }
        if (!start_from_game(s, memory)) {
            std::cout << "[freecam] no game camera to start from here" << std::endl;
            return;
        }
        // MHP2G_FREE_CAMERA_POSE=x,y,z,yaw,pitch starts from there instead,
        // so two runs can look from exactly the same place.
        if (const char *text = std::getenv("MHP2G_FREE_CAMERA_POSE"); text != nullptr && *text != '\0') {
            FreePose pose;
            if (std::sscanf(text, "%f,%f,%f,%f,%f", &pose.eye[0], &pose.eye[1], &pose.eye[2], &pose.yaw, &pose.pitch) ==
                5) {
                pose.pitch = std::clamp(pose.pitch, -kFreePitchLimit, kFreePitchLimit);
                s.pose = pose;
                s.view = view_of_pose(pose);
            } else {
                std::cout << "[freecam] MHP2G_FREE_CAMERA_POSE wants x,y,z,yaw,pitch" << std::endl;
            }
        }
        s.active = true;
        s.paused = false;
        s.speed = settings::current().free_camera_speed;
        s.moved = s.other = s.last_moved = s.last_other = 0u;
        say_pose("on", s.pose, s.speed);
        return; // the first frame shows exactly the game's view
    }
    if (!s.active) return;
    if (request.reset && start_from_game(s, memory)) say_pose("back to the game's camera", s.pose, s.speed);
    if (request.pause) {
        s.paused = !s.paused;
        std::cout << (s.paused ? "[freecam] photo mode: the game stands still" : "[freecam] photo mode off")
                  << std::endl;
    }
    if (request.speed_steps != 0) {
        s.speed = std::clamp(s.speed * std::pow(kSpeedStep, static_cast<float>(request.speed_steps)),
            settings::kMinFreeCameraSpeed, settings::kMaxFreeCameraSpeed);
    }
    const float factor = (request.fast ? kFastFactor : 1.0f) * (request.slow ? kSlowFactor : 1.0f);
    s.pose = fly(s.pose, request.input, seconds, s.speed * factor);
    s.view = view_of_pose(s.pose);
}

std::function<void(gpu::DrawCall &)> free_camera_view_hook(const psprecomp::GuestMemory &memory) {
    State &s = state();
    const bool tracing = trace_every() != 0u;
    if (!s.active && !tracing) return {};
    // The game's view as it is while this list runs.
    const std::optional<Matrix> game = s.active ? game_view(memory) : std::nullopt;
    return [&s, tracing, game](gpu::DrawCall &call) {
        if (tracing) observe(s, call);
        if (!s.active) return;
        if (game && same_uploaded(call.view, *game)) {
            call.view = s.view;
            ++s.moved;
        } else {
            ++s.other;
        }
    };
}

void free_camera_frame_end(psprecomp::Runtime &runtime) {
    State &s = state();
    if (trace_every() != 0u) {
        if (s.frame % trace_every() == 0u) print_views(runtime.memory(), s);
        s.views.clear();
    }
    s.last_moved = s.moved;
    s.last_other = s.other;
    s.moved = s.other = 0u;
    ++s.frame;
}

bool FrameStepRepeat::update(bool held, Clock::time_point now) {
    if (!held) {
        held_ = false;
        return false;
    }
    if (!held_) {
        held_ = true;
        next_ = now + kFrameStepDelay;
        return true;
    }
    if (now < next_) return false;
    // A step that came late does not bunch the next ones up.
    next_ += kFrameStepRepeat;
    if (next_ <= now) next_ = now + kFrameStepRepeat;
    return true;
}

} // namespace mhp2g::camera
