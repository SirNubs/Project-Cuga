#include "camera/lock_on.hpp"

#include "psprecomp/runtime.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <utility>

namespace mhp3rd::camera {
namespace {

using namespace lock_on_layout;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kUnitsPerTurn = 65536.0f;
constexpr float kDegrees = 180.0f / kPi;

// The overlay header: "MWo3", its load address at +8, its name at +32.
constexpr std::uint32_t kOverlayMagic = 0x336F574Du;
constexpr std::string_view kQuestOverlay = "game_task.ovl";

// The camera object (camera/free_camera.hpp): the pointer to it, its view
// matrix and, 0x80 on, its projection, both column major. Found beside the
// view in a dump: a perspective of 480/272's shape with the near plane at 30.
constexpr std::uint32_t kCameraObject = 0x08A2F958u;
constexpr std::uint32_t kViewMatrix = 0xF50u;
constexpr std::uint32_t kProjection = 0xFD0u;

// Farther than this from the camera's look-at point, horizontally, a lock
// lets go: well past what the camera can frame. Areas are a few thousand
// units across.
constexpr float kFarthest = 6000.0f;
// Where on the monster the camera aims: a little above its feet, which is
// where the game keeps its position.
constexpr float kAimHeight = 100.0f;
// Turning by hand lets go once this many degrees have built up, with what
// came before fading by kManualFade each update, so a stray nudge of the
// mouse does not.
constexpr float kManualRelease = 6.0f;
constexpr float kManualFade = 0.7f;
// How the yaw follows: a share of the rest each update (30 a second), at
// least kSmallestStep and at most kLargestStep units.
constexpr float kYawShare = 0.2f;
constexpr int kSmallestStep = 48;  // about a quarter of a degree
constexpr int kLargestStep = 2048; // 11.25 degrees
// And the pitch: the eye rises or sinks by half the monster's elevation as
// seen from the look-at point, at most kPitchUp or kPitchDown from where it
// was at the lock, a share of the rest each update.
constexpr float kPitchFollow = 0.5f;
constexpr float kPitchUp = 10.0f;
constexpr float kPitchDown = 20.0f;
constexpr float kPitchShare = 0.12f;
constexpr float kPitchLowest = -30.0f;
constexpr float kPitchHighest = 60.0f;
// Nearer than kNear to the camera's look-at point, horizontally, the
// monster is on top of the hunter and its direction means little: the yaw
// holds. Beyond, the turn comes in over kNearBlend.
constexpr float kNear = 300.0f;
constexpr float kNearBlend = 400.0f;
// Past this much of a turn to go, the turn keeps the way it was going.
constexpr int kHalfTurnBand = 28672; // 157.5 degrees
// A first tap that no camera update took in this many flips is dropped.
constexpr unsigned kTapFrames = 6u;

struct State {
    bool suspended{};
    bool tap{};
    unsigned tap_age{};
    bool locked{};
    std::uint32_t target{};
    std::vector<std::uint32_t> visited;
    float manual{};
    int turning{}; // the way the last step went: +1, -1 or 0
    bool pitch_known{};
    float base_pitch{};
    float pitch{};
    std::uint64_t updates{};
    std::string note;
};
State state;

bool trace_on() {
    static const bool on = std::getenv("MHP3RD_TRACE_LOCK_ON") != nullptr;
    return on;
}

float load_float(const psprecomp::GuestMemory &memory, std::uint32_t address) {
    return std::bit_cast<float>(memory.load32(address));
}

Vec3 load_vec(const psprecomp::GuestMemory &memory, std::uint32_t address) {
    return {load_float(memory, address), load_float(memory, address + 4u), load_float(memory, address + 8u)};
}

bool finite(const Vec3 &v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool on_quest(const psprecomp::GuestMemory &memory) {
    if (memory.raw_pointer(kTaskSlot, 64u) == nullptr || memory.load32(kTaskSlot) != kOverlayMagic ||
        memory.load32(kTaskSlot + 8u) != kTaskSlot)
        return false;
    for (std::size_t i = 0; i < kQuestOverlay.size(); ++i)
        if (memory.load8(kTaskSlot + 32u + static_cast<std::uint32_t>(i)) !=
            static_cast<std::uint8_t>(kQuestOverlay[i]))
            return false;
    return memory.load8(kTaskSlot + 32u + static_cast<std::uint32_t>(kQuestOverlay.size())) == 0u;
}

float horizontal(const Vec3 &from, const Vec3 &to) {
    return std::hypot(to.x - from.x, to.z - from.z);
}

const LockMonster *find(const std::vector<LockMonster> &monsters, std::uint32_t address) {
    for (const LockMonster &m : monsters)
        if (m.address == address) return &m;
    return nullptr;
}

void lock(std::uint32_t target, const char *how) {
    state.locked = true;
    state.target = target;
    state.manual = 0.0f;
    state.turning = 0;
    state.visited.push_back(target);
    std::cout << "[lockon] " << how << " the monster at 0x" << std::hex << target << std::dec << std::endl;
}

} // namespace

std::vector<LockMonster> lock_on_monsters(const psprecomp::GuestMemory &memory) {
    std::vector<LockMonster> out;
    if (!on_quest(memory) || memory.raw_pointer(kMonsterTable, kMonsterSlots * 4u) == nullptr) return out;
    for (std::uint32_t slot = 0; slot < kMonsterSlots; ++slot) {
        const std::uint32_t at = memory.load32(kMonsterTable + slot * 4u);
        if (at < 0x08800000u || memory.raw_pointer(at, kExtent) == nullptr) continue;
        const auto most = static_cast<std::int16_t>(memory.load16(at + kMostHealth));
        const auto health = static_cast<std::int16_t>(memory.load16(at + kHealth));
        if (most <= 0 || health <= 0) continue; // a companion, not spawned yet, or dead
        if ((memory.load32(at + kFlags) & kOtherArea) != 0u) continue;
        const Vec3 position = load_vec(memory, at + kPosition);
        if (!finite(position)) continue;
        out.push_back({at, slot, position});
    }
    return out;
}

std::uint16_t yaw_towards(const Vec3 &from, const Vec3 &to) {
    // The camera looks along -(sin yaw, cos yaw); the eye sits the other way.
    const float angle = std::atan2(-(to.x - from.x), -(to.z - from.z));
    const long units = std::lround(angle / (2.0f * kPi) * kUnitsPerTurn);
    return static_cast<std::uint16_t>(units & 0xFFFF);
}

int yaw_difference(std::uint16_t from, std::uint16_t to) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(to - from));
}

std::optional<std::uint32_t> lock_on_pick(const std::vector<LockMonster> &monsters, const Vec3 &look_at,
    std::uint16_t camera_yaw, std::optional<std::uint32_t> current, const std::vector<std::uint32_t> &visited) {
    if (monsters.empty()) return std::nullopt;
    if (!current) {
        const LockMonster *best = nullptr;
        bool best_in_view = false;
        float best_distance = 0.0f;
        for (const LockMonster &m : monsters) {
            const int off = yaw_difference(camera_yaw, yaw_towards(look_at, m.position));
            const bool in_view = std::abs(static_cast<float>(off)) * 360.0f / kUnitsPerTurn <= kInView;
            const float distance = horizontal(look_at, m.position);
            if (best == nullptr || (in_view && !best_in_view) ||
                (in_view == best_in_view && distance < best_distance)) {
                best = &m;
                best_in_view = in_view;
                best_distance = distance;
            }
        }
        return best->address;
    }
    // The next one in the table after the current one, round the table,
    // that this lock has not been on.
    std::uint32_t from = 0u;
    for (const LockMonster &m : monsters)
        if (m.address == *current) from = m.slot;
    for (std::uint32_t step = 1; step <= kMonsterSlots; ++step) {
        const std::uint32_t slot = (from + step) % kMonsterSlots;
        for (const LockMonster &m : monsters)
            if (m.slot == slot && m.address != *current &&
                std::find(visited.begin(), visited.end(), m.address) == visited.end())
                return m.address;
    }
    return std::nullopt;
}

std::uint16_t lock_on_ease_yaw(std::uint16_t current, std::uint16_t wanted, float weight, int sign) {
    weight = std::clamp(weight, 0.0f, 1.0f);
    if (weight <= 0.0f) return current;
    int rest = yaw_difference(current, wanted);
    if (std::abs(rest) <= kSmallestStep) return wanted;
    if (std::abs(rest) > kHalfTurnBand && sign != 0 && (rest > 0) != (sign > 0))
        rest += sign > 0 ? 65536 : -65536; // the long way, as before
    const int way = rest > 0 ? 1 : -1;
    const float share = static_cast<float>(std::abs(rest)) * kYawShare * weight;
    const int step = std::clamp(static_cast<int>(std::lround(share)), kSmallestStep,
        std::max(kSmallestStep, static_cast<int>(static_cast<float>(kLargestStep) * weight)));
    return static_cast<std::uint16_t>(current + way * std::min(step, std::abs(rest)));
}

void lock_on_tap() {
    state.tap = true;
    state.tap_age = 0u;
}

std::optional<LockOnAim> lock_on_update(const psprecomp::GuestMemory &memory, const LockOnCamera &camera) {
    if (state.suspended) return std::nullopt;
    ++state.updates;
    const bool tapped = std::exchange(state.tap, false);
    if (!tapped && !state.locked) return std::nullopt;
    const std::vector<LockMonster> monsters = lock_on_monsters(memory);
    if (tapped) {
        const std::optional<std::uint32_t> current = state.locked ? std::optional(state.target) : std::nullopt;
        if (!state.locked) state.visited.clear();
        const std::optional<std::uint32_t> next =
            lock_on_pick(monsters, camera.look_at, camera.yaw, current, state.visited);
        if (!next) {
            if (state.locked) {
                lock_on_release("");
            } else {
                state.note = "No large monster in this area";
                std::cout << "[lockon] no large monster in this area" << std::endl;
            }
            return std::nullopt;
        }
        const bool first = !state.locked;
        lock(*next, first ? "locked onto" : "moved to");
        if (first) {
            state.pitch_known = true;
            state.base_pitch = camera.pitch;
            state.pitch = camera.pitch;
        }
    }
    const LockMonster *target = find(monsters, state.target);
    if (target == nullptr) {
        lock_on_release("Lock-on: the monster is gone");
        return std::nullopt;
    }
    if (horizontal(camera.look_at, target->position) > kFarthest) {
        lock_on_release("Lock-on: the monster is too far");
        return std::nullopt;
    }
    // The player takes the camera back: a camera command of the game's, or
    // turning it by hand.
    state.manual = state.manual * kManualFade + std::fabs(camera.manual_yaw);
    if (camera.command || state.manual > kManualRelease) {
        std::cout << "[lockon] the player took the camera back ("
                  << (camera.command ? "a camera command" : "turned by hand") << ")" << std::endl;
        lock_on_release("");
        return std::nullopt;
    }
    const Vec3 aim{target->position.x, target->position.y + kAimHeight, target->position.z};
    const std::uint16_t wanted = yaw_towards(camera.look_at, aim);
    const float near = (horizontal(camera.look_at, aim) - kNear) / kNearBlend;
    LockOnAim out{lock_on_ease_yaw(camera.yaw, wanted, near, state.turning), std::nullopt};
    const int step = yaw_difference(camera.yaw, out.yaw);
    state.turning = step > 0 ? 1 : (step < 0 ? -1 : 0);
    if (!camera.player_pitch && state.pitch_known) {
        const float distance = std::max(horizontal(camera.look_at, aim), 1.0f);
        const float elevation = std::atan2(aim.y - camera.look_at.y, distance) * kDegrees;
        const float goal = std::clamp(
            state.base_pitch - elevation * kPitchFollow, state.base_pitch - kPitchDown, state.base_pitch + kPitchUp);
        state.pitch += (std::clamp(goal, kPitchLowest, kPitchHighest) - state.pitch) * kPitchShare;
        out.pitch = state.pitch;
    } else {
        state.pitch_known = false;
    }
    if (trace_on())
        std::cout << "[lockon] update " << state.updates << " target 0x" << std::hex << state.target << std::dec
                  << " at " << target->position.x << "," << target->position.y << "," << target->position.z
                  << " look-at " << camera.look_at.x << "," << camera.look_at.y << "," << camera.look_at.z << " yaw "
                  << camera.yaw << " -> " << out.yaw << " (wanted " << wanted << ") pitch " << camera.pitch << " -> "
                  << (out.pitch ? *out.pitch : camera.pitch) << " manual " << state.manual << std::endl;
    return out;
}

void lock_on_suspend(bool suspended) {
    state.suspended = suspended;
}

void lock_on_frame(const psprecomp::GuestMemory &memory) {
    if (state.suspended) state.tap_age = 0u;
    // A tap while locked moves on or lets go, which needs no camera: done
    // here, so it counts even while the follow camera does not run (a
    // grab, an aim).
    if (state.tap && state.locked && !state.suspended) {
        state.tap = false;
        const std::vector<LockMonster> monsters = lock_on_monsters(memory);
        const std::optional<std::uint32_t> next =
            lock_on_pick(monsters, {}, 0u, std::optional(state.target), state.visited);
        if (next)
            lock(*next, "moved to");
        else
            lock_on_release("");
    }
    if (state.tap && ++state.tap_age > kTapFrames) {
        // No follow camera took it: the village, a cutscene, aiming.
        state.tap = false;
        if (!state.locked) {
            state.note = "Lock-on works in quests, with the camera following the hunter";
            std::cout << "[lockon] tap with no follow camera to turn" << std::endl;
        }
    }
    if (!state.locked) return;
    const std::vector<LockMonster> monsters = lock_on_monsters(memory);
    if (find(monsters, state.target) == nullptr) lock_on_release("Lock-on: the monster is gone");
}

bool lock_on_wanted() {
    return state.tap || state.locked;
}

LockOnStatus lock_on_status() {
    return {state.locked, state.locked ? state.target : 0u};
}

std::optional<std::array<float, 2>> lock_on_marker(const psprecomp::GuestMemory &memory) {
    if (!state.locked || memory.raw_pointer(kCameraObject, 4u) == nullptr) return std::nullopt;
    const std::uint32_t camera = memory.load32(kCameraObject);
    if (memory.raw_pointer(camera, kProjection + 64u) == nullptr ||
        memory.raw_pointer(state.target, kExtent) == nullptr)
        return std::nullopt;
    const Vec3 p = load_vec(memory, state.target + kPosition);
    const float point[4] = {p.x, p.y + kAimHeight, p.z, 1.0f};
    float eye[4]{};
    float clip[4]{};
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            eye[row] +=
                load_float(memory, camera + kViewMatrix + static_cast<std::uint32_t>((col * 4 + row) * 4)) * point[col];
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            clip[row] +=
                load_float(memory, camera + kProjection + static_cast<std::uint32_t>((col * 4 + row) * 4)) * eye[col];
    if (!std::isfinite(clip[0]) || !std::isfinite(clip[1]) || !std::isfinite(clip[3]) || clip[3] <= 1.0f)
        return std::nullopt;
    const float x = (clip[0] / clip[3] + 1.0f) * 0.5f;
    const float y = (1.0f - clip[1] / clip[3]) * 0.5f;
    return std::array<float, 2>{std::clamp(x, 0.0f, 1.0f), std::clamp(y, 0.0f, 1.0f)};
}

void lock_on_release(const char *why) {
    if (state.locked)
        std::cout << "[lockon] let go of the monster at 0x" << std::hex << state.target << std::dec
                  << (why != nullptr && *why != '\0' ? std::string(": ") + why : std::string()) << std::endl;
    if (state.locked && why != nullptr && *why != '\0') state.note = why;
    state.locked = false;
    state.target = 0u;
    state.manual = 0.0f;
    state.turning = 0;
    state.pitch_known = false;
}

void lock_on_reset() {
    state = State{};
}

std::string lock_on_take_note() {
    return std::exchange(state.note, std::string());
}

} // namespace mhp3rd::camera
