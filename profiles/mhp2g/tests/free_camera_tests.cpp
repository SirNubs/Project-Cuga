// The free camera's geometry and its guard: no game data is needed. The
// view matrix below is one the game drew a quest area with, as
// MHP3RD_TRACE_VIEWS printed it.
#include "camera/free_camera.hpp"
#include "gpu/ge_state.hpp"
#include "psprecomp/runtime.hpp"

#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
using namespace mhp3rd::camera;

constexpr std::uint32_t kCameraPointer = 0x08A2F958u;
constexpr std::uint32_t kCamera = 0x08900000u;
int failures{};

void check(bool condition, const char *message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool near(float a, float b, float slack = 1e-3f) {
    return std::fabs(a - b) <= slack;
}

// Misty Peaks, base camp: rows of the rotation, then the translation.
Matrix traced_view() {
    Matrix view{};
    const float rows[3][3] = {{0.7660f, 0.0000f, -0.6429f}, {0.0497f, 0.9970f, 0.0592f}, {0.6409f, -0.0772f, 0.7637f}};
    for (int row = 0; row < 3; ++row)
        for (int axis = 0; axis < 3; ++axis) view[static_cast<std::size_t>(axis * 4 + row)] = rows[row][axis];
    view[12] = 223.1641f;
    view[13] = -55.5488f;
    view[14] = -831.5312f;
    view[15] = 1.0f;
    return view;
}

void test_traced_view() {
    const auto pose = pose_of_view(traced_view());
    check(pose.has_value(), "the game's view is a rotation");
    if (!pose) return;
    // Where MHP3RD_TRACE_VIEWS put the eye.
    check(near(pose->eye[0], 364.79f, 0.2f) && near(pose->eye[1], -8.84f, 0.2f) && near(pose->eye[2], 781.77f, 0.2f),
        "the eye is where the trace put it");
    const Matrix again = view_of_pose(*pose);
    bool same = true;
    for (std::size_t i = 0; i < 16u; ++i) same = same && near(again[i], traced_view()[i], i >= 12u ? 0.5f : 2e-3f);
    check(same, "the pose gives the game's view back");
}

void test_round_trip() {
    for (const FreePose pose : {FreePose{{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f},
             FreePose{{100.0f, -50.0f, 7.0f}, 90.0f, 30.0f}, FreePose{{-3000.0f, 200.0f, 5000.0f}, -135.0f, -80.0f}}) {
        const auto back = pose_of_view(view_of_pose(pose));
        check(back.has_value(), "a pose's view is a rotation");
        if (!back) continue;
        check(near(back->yaw, pose.yaw, 1e-2f) && near(back->pitch, pose.pitch, 1e-2f), "yaw and pitch come back");
        check(near(back->eye[0], pose.eye[0], 1e-2f) && near(back->eye[1], pose.eye[1], 1e-2f) &&
                near(back->eye[2], pose.eye[2], 1e-2f),
            "the eye comes back");
    }
}

void test_not_a_view() {
    Matrix scaled = traced_view();
    for (std::size_t i : {0u, 1u, 2u, 4u, 5u, 6u, 8u, 9u, 10u}) scaled[i] *= 2.0f;
    check(!pose_of_view(scaled).has_value(), "a scaled matrix is not a view");
    Matrix mirrored = traced_view();
    for (std::size_t i : {0u, 4u, 8u}) mirrored[i] = -mirrored[i];
    check(!pose_of_view(mirrored).has_value(), "a mirrored matrix is not a view");
    check(!pose_of_view(Matrix{}).has_value(), "nothing is not a view");
}

void test_fly() {
    const FreePose start{{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f};
    FlyInput ahead;
    ahead.forward = 1.0f;
    const FreePose moved = fly(start, ahead, 0.5f, 400.0f);
    check(near(moved.eye[2], 200.0f) && near(moved.eye[0], 0.0f) && near(moved.eye[1], 0.0f),
        "forward goes along the look, at the speed");

    // A turn to the right looks where the right strafe went.
    FlyInput strafe;
    strafe.right = 1.0f;
    const FreePose right = fly(start, strafe, 1.0f, 100.0f);
    FlyInput turn;
    turn.yaw_degrees = 90.0f;
    FreePose turned = fly(start, turn, 0.0f, 100.0f);
    turned = fly(turned, ahead, 1.0f, 100.0f);
    check(near(turned.eye[0], right.eye[0], 0.05f) && near(turned.eye[2], right.eye[2], 0.05f),
        "turning right faces where strafing right goes");

    // Positive pitch looks down, and stops short of straight down.
    FlyInput down;
    down.pitch_degrees = 30.0f;
    check(near(fly(start, down, 0.0f, 0.0f).pitch, -30.0f), "positive pitch looks down");
    down.pitch_degrees = 500.0f;
    check(near(fly(start, down, 0.0f, 0.0f).pitch, -kFreePitchLimit), "pitch stops at the limit");

    FlyInput rise;
    rise.up = 1.0f;
    rise.forward = 1.0f;
    rise.right = 1.0f;
    const FreePose up = fly(start, rise, 1.0f, 10.0f);
    check(near(up.eye[1], 10.0f), "up is straight up");
    check(near(std::hypot(up.eye[0], up.eye[2]), 10.0f), "a diagonal is no faster than a straight line");
}

void test_same_uploaded() {
    const Matrix kept = traced_view();
    Matrix uploaded = kept;
    for (float &value : uploaded) value = std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) & 0xFFFFFF00u);
    uploaded[3] = 5.0f; // never uploaded
    check(same_uploaded(uploaded, kept), "the GE's 24 bits of the game's view are the game's view");
    uploaded[12] += 1.0f;
    check(!same_uploaded(uploaded, kept), "another translation is another view");
}

void test_game_view_and_guard() {
    psprecomp::Runtime runtime;
    auto &memory = runtime.memory();
    check(!game_view(memory).has_value(), "no camera object, no view");
    memory.store32(kCameraPointer, kCamera);
    const auto store = [&](std::uint32_t offset, float value) {
        memory.store32(kCamera + offset, std::bit_cast<std::uint32_t>(value));
    };
    store(0x0u, 30.0f);
    store(0x4u, 65000.0f);
    store(0x8u, 480.0f / 272.0f);
    store(0xCu, 0.8722222f);
    const Matrix view = traced_view();
    for (std::uint32_t i = 0; i < 16u; ++i) store(0xF50u + i * 4u, view[i]);
    const auto found = game_view(memory);
    check(found.has_value() && *found == view, "the camera object's view is the game's view");
    store(0xCu, 0.0f);
    check(!game_view(memory).has_value(), "a camera object that does not look like one is left alone");

    // Off (and not tracing): the display lists get no hook at all, so no
    // draw is looked at, let alone changed.
    check(!free_camera_active(), "the free camera starts off");
    if (std::getenv("MHP3RD_TRACE_VIEWS") == nullptr)
        check(!free_camera_view_hook(memory), "off, nothing is hooked into the display lists");
}

// Frame step (#187): once on the press, then after a delay at a steady
// rate while held, never more than one step per call.
void test_frame_step_repeat() {
    using namespace std::chrono_literals;
    FrameStepRepeat repeat;
    const FrameStepRepeat::Clock::time_point t0{};
    check(!repeat.update(false, t0), "nothing while the bind is up");
    check(repeat.update(true, t0), "a press steps at once");
    check(!repeat.update(true, t0 + 16ms) && !repeat.update(true, t0 + kFrameStepDelay - 1ms),
        "held, nothing more until the delay is over");
    check(repeat.update(true, t0 + kFrameStepDelay), "then a step");
    check(!repeat.update(true, t0 + kFrameStepDelay + 50ms), "and none until the repeat is due");
    check(repeat.update(true, t0 + kFrameStepDelay + kFrameStepRepeat), "then the next");
    // A frame that took long (a slow machine) does not bunch steps up.
    const auto late = t0 + kFrameStepDelay + 10 * kFrameStepRepeat;
    check(repeat.update(true, late) && !repeat.update(true, late + 1ms), "a late step is one step");
    int steps = 0;
    FrameStepRepeat held;
    for (auto t = t0; t < t0 + 1400ms; t += 16ms) steps += held.update(true, t) ? 1 : 0;
    check(steps == 1 + static_cast<int>((1400ms - kFrameStepDelay) / kFrameStepRepeat) ||
            steps == static_cast<int>((1400ms - kFrameStepDelay) / kFrameStepRepeat),
        "held for 1.4 s at 60 frames a second, about ten steps a second after the delay");
    check(!held.update(false, t0 + 1400ms) && held.update(true, t0 + 1416ms), "released and pressed again: a step");
    held.reset();
    check(held.update(true, t0 + 1432ms), "reset forgets the hold");
}

} // namespace

int main() {
    test_frame_step_repeat();
    test_traced_view();
    test_round_trip();
    test_not_a_view();
    test_fly();
    test_same_uploaded();
    test_game_view_and_guard();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "free camera tests passed\n";
    return 0;
}
