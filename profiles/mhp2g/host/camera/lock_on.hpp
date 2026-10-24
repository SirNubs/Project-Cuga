#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace psprecomp {
class GuestMemory;
class Runtime;
}

// Lock-on (#163): a tap of the Lock on bind turns the quest camera towards a
// large monster and keeps it there while the monster moves.
//
// It is not a camera of its own. The follow camera's driver (game_camera.hpp)
// asks it, on every update of the game's ordinary follow camera, which yaw
// and pitch the camera should move towards, and moves the game's own angles
// there the way the analog camera does. The game keeps everything else: its
// collision checks, its presets for every other mode, and the hunter's
// movement, which stays relative to the camera as it always is.
//
// ULJM05500 (also under the FUC patch), as read in a quest's memory:
// - The quest's monsters, large and small, are a table of up to twenty
//   pointers at 0x09C0D3C0 in the quest overlay's data; a monster that is
//   gone leaves an empty slot. Each object starts with its class pointer and
//   keeps its position as three floats at +0x40 and its health (s16) at
//   +0x2E4: 320 for a Giadrome, 89 for a Popo, and 0 for that Popo killed.
// - Bit 0x4 of the word at +0x4 of the object is set while the monster is in
//   an area other than the hunter's: 7 for a Giadrome elsewhere (the hunter
//   in area 1), 3 or 1 for the Popo beside the hunter.
// - Bit 0x40 of the word at +0x27C was set for the Giadrome and clear for
//   the three small monsters of that quest: taken as the mark of a large
//   monster, which a tap prefers. One quest's worth of evidence.
// - Each area has its own coordinates, and neighbouring areas overlap, so a
//   monster in another area can seem near; distance alone does not do.
// - The camera looks from its eye at +0x0 of the camera structure (s1 of the
//   camera update) to its look-at point at +0x10. Its yaw, in 65536ths of a
//   turn, puts the eye in the direction (sin yaw, cos yaw) from the look-at
//   point, so the camera looks along -(sin yaw, cos yaw). Checked in two
//   quests: yaw 7283 (40.0 degrees) with the eye at 40.0 degrees, 32768 with
//   the eye at 180.
namespace mhp2g::camera {

struct Vec3 {
    float x{};
    float y{};
    float z{};
};

// What lock-on reads in the game. Exposed for tests, which build a stand-in.
namespace lock_on_layout {
inline constexpr std::uint32_t kTaskSlot = 0x09A5A580u; // the code overlay slot: "game_task.ovl" in a quest
inline constexpr std::uint32_t kMonsterTable = 0x09C0D3C0u;
inline constexpr std::uint32_t kMonsterSlots = 20u;
inline constexpr std::uint32_t kFlags = 0x04u;
inline constexpr std::uint32_t kOtherArea = 0x4u;
inline constexpr std::uint32_t kPosition = 0x40u;
inline constexpr std::uint32_t kHealth = 0x2E4u;
inline constexpr std::uint32_t kTraits = 0x27Cu;
inline constexpr std::uint32_t kLarge = 0x40u;
inline constexpr std::uint32_t kExtent = 0x2E8u;
} // namespace lock_on_layout

struct LockMonster {
    std::uint32_t address{};
    std::uint32_t slot{};
    Vec3 position;
    bool large{};
};

// The monsters that can be locked onto: in the monster table while a quest
// runs, alive, and in the hunter's area.
[[nodiscard]] std::vector<LockMonster> lock_on_monsters(const psprecomp::GuestMemory &memory);

// The camera's yaw, in its units, that looks from `from` towards `to`.
[[nodiscard]] std::uint16_t yaw_towards(const Vec3 &from, const Vec3 &to);
// `to` minus `from`, the short way round: -32768..32767.
[[nodiscard]] int yaw_difference(std::uint16_t from, std::uint16_t to);

// What a tap does, given the monsters there are now, the camera (its look-at
// point and yaw), the target locked now, if any, and the ones this lock has
// already gone through. Returns the monster to lock onto, or nothing to let
// go. With nothing locked: a large monster before a small one, then the
// nearest within kInView of where the camera looks, else the nearest one.
// Locked: the next monster in the table this lock has not been on yet, the
// large ones first; after the last, nothing.
inline constexpr float kInView = 60.0f; // degrees either side of where the camera looks
[[nodiscard]] std::optional<std::uint32_t> lock_on_pick(const std::vector<LockMonster> &monsters, const Vec3 &look_at,
    std::uint16_t camera_yaw, std::optional<std::uint32_t> current, const std::vector<std::uint32_t> &visited);

// One step of the camera's yaw towards `wanted`: a share of what is left,
// neither too small to finish nor so large that the turn jumps. `weight`
// (0..1) scales the step, down to none; `sign` (+1, -1, 0 for none) is the
// way the previous step went, kept while the rest is near half a turn, where
// the short way round flips from one update to the next.
[[nodiscard]] std::uint16_t lock_on_ease_yaw(
    std::uint16_t current, std::uint16_t wanted, float weight = 1.0f, int sign = 0);

// --- The running lock -------------------------------------------------------

// A tap of the bind, from the flip.
void lock_on_tap();

// What the follow camera's driver hands over on each update, and what it
// gets back: the yaw to put the camera at, and the pitch (degrees, the eye
// above the look-at point) when lock-on tilts it too.
struct LockOnCamera {
    std::uint32_t address{}; // the camera structure (s1)
    std::uint16_t yaw{};     // the camera's yaw now
    Vec3 look_at;
    // The player turned the camera by hand this update, in degrees, or gave
    // the game a camera command (the D-pad's turn or tilt, recentring).
    float manual_yaw{};
    bool command{};
    // The camera's pitch now, from the game's eye offset, in degrees.
    float pitch{};
    bool player_pitch{}; // the player has tilted the camera by hand
};
struct LockOnAim {
    std::uint16_t yaw{};
    std::optional<float> pitch;
};
// Nothing when there is no lock, or it has just been let go.
[[nodiscard]] std::optional<LockOnAim> lock_on_update(const psprecomp::GuestMemory &memory, const LockOnCamera &camera);

// While the free camera flies (camera/free_camera.hpp) a lock is held but
// does not turn the game's camera, and taps wait.
void lock_on_suspend(bool suspended);

// Once a flip: lets a lock go whose monster is no longer there (killed,
// captured, gone to another area, the quest over), and a tap that no camera
// update took (the village, a cutscene).
void lock_on_frame(const psprecomp::GuestMemory &memory);

// A tap is waiting for a camera update, or a lock is on: the camera driver
// must be in place for either.
[[nodiscard]] bool lock_on_wanted();

struct LockOnStatus {
    bool locked{};
    std::uint32_t target{};
};
[[nodiscard]] LockOnStatus lock_on_status();

// The locked monster's place on the game's picture, each coordinate 0..1
// from the top left, from the game's own view and projection; nothing when
// there is no lock or the monster is behind the camera. For the marker.
[[nodiscard]] std::optional<std::array<float, 2>> lock_on_marker(const psprecomp::GuestMemory &memory);

// "Monsters on the map": while a quest runs, keeps the hunter's own Psychoserum
// effect from running out, so the game marks the large monsters on its map as
// it does after that item. The effect is a count-down byte at +0x62A of the
// hunter's object (0x090AF180 in a quest; the US game's "Auto-Paint Bosses"
// code, 0x4400 lower here as the sharpness values are). Nothing of a monster
// is touched. Once a flip.
void monster_map_frame(psprecomp::GuestMemory &memory, bool on);

// "Map without the Map item": the game asks one function whether the hunter may see the quest map (0x08844604
// in the English-patched executable: a skill, or item 863, the Map, in the pouch). With the setting on, the
// answer is yes. Only this player's own map is concerned. Call once after the profile is installed; does
// nothing when the executable has other code at that address.
void prepare_always_map(psprecomp::Runtime &runtime);

// "Restock": fills each kind of item in the pouch up to the most the hunter may carry, taking the items out of
// the item box. Only the player's own data is touched (pouch and box, in the village), so nothing of it concerns
// other players. It is a button of the item box: restock_offered() says whether the box's own first menu is up
// (not one of its screens, where the buttons are taken), and restock_frame(), once a flip, restocks when the
// button was pressed there and returns a line to show, or nothing.
// The box's menu: the game's menu number at 0x09B0AC38 is 24 with 0x09B0AC40 and 0x09995250 at 1, and
// 0x09B154F8 is 0 until one of the box's screens opens (found by comparing memory; village house).
// Player data: pointer at 0x089C7508; pouch 24 x (u16 item, s16 count) at +0x6AEA6 (the game's own accessor
// 0x088567AC); item box 1000 of the same at +0x3428. Item table: 24 bytes an item from 0x0899A228, the most
// one may carry in byte 19. English-patched executable only (checked by prepare_always_map).
bool restock_offered(const psprecomp::GuestMemory &memory);
std::string restock_frame(psprecomp::GuestMemory &memory, bool pressed);

// "Hold Select to skip the wait after a quest": once a quest's goal is met the
// game counts frames down at 0x09A02210 (0 during the quest; seen running
// down to the reward screen) before it ends the quest. With `allowed` and
// Select held for a second, the count is cut to its last frame. Once a flip;
// the caller keeps `allowed` off during ad hoc play, where every player's
// game counts for itself.
void quest_skip_frame(psprecomp::GuestMemory &memory, bool allowed, bool select_held);

// Lets go, for tests and when the setting goes off. `why` goes to the
// trace (MHP2G_TRACE_LOCK_ON) and, when not empty, to a note on screen.
void lock_on_release(const char *why);
void lock_on_reset();

// Why the last tap did nothing, once, for a note on screen: "" when there is
// none to show.
[[nodiscard]] std::string lock_on_take_note();

} // namespace mhp2g::camera
