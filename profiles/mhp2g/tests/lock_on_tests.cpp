// Lock-on (camera/lock_on.hpp) without game code or game data: a stand-in
// for the quest overlay's header, the monster table and the camera, and the
// real camera driver dispatching the rotation helper.
#include "camera/camera_input.hpp"
#include "camera/game_camera.hpp"
#include "camera/lock_on.hpp"
#include "input/bindings.hpp"
#include "input/presets.hpp"
#include "settings/settings.hpp"
#include "psprecomp/runtime.hpp"

#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

namespace mhp3rd::settings {
Settings &current() {
    static Settings settings;
    return settings;
}
}

namespace {
using namespace mhp3rd::camera;
namespace game = mhp3rd::camera::lock_on_layout;

constexpr std::uint32_t helper = 0x08878B70u;
constexpr std::uint32_t return_pc = 0x088E626Cu;
constexpr std::uint32_t camera_address = 0x08900000u;
constexpr std::uint32_t stack_address = 0x08901000u;
constexpr std::uint32_t preset_address = 0x08902000u;
constexpr std::uint32_t monster_a = 0x09000000u;
constexpr std::uint32_t monster_b = 0x09001000u;
constexpr std::uint32_t companion = 0x09002000u;
int failures{};

void check(bool condition, const char *message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void original(psprecomp::Runtime &, psprecomp::AllegrexContext &ctx) {
    ctx.pc = ctx.gpr[31];
}

void write_float(psprecomp::GuestMemory &memory, std::uint32_t address, float value) {
    memory.store32(address, std::bit_cast<std::uint32_t>(value));
}

void place(psprecomp::GuestMemory &memory, std::uint32_t monster, float x, float z, std::int16_t health = 1000,
    std::int16_t most = 1000, bool other_area = false) {
    write_float(memory, monster + game::kPosition, x);
    write_float(memory, monster + game::kPosition + 4u, 0.0f);
    write_float(memory, monster + game::kPosition + 8u, z);
    memory.store16(monster + game::kHealth, static_cast<std::uint16_t>(health));
    memory.store16(monster + game::kMostHealth, static_cast<std::uint16_t>(most));
    memory.store32(monster + game::kFlags, other_area ? 7u : 3u);
}

// The quest overlay's header, as the game has it in the task slot.
void start_quest(psprecomp::GuestMemory &memory) {
    memory.store32(game::kTaskSlot, 0x336F574Du);
    memory.store32(game::kTaskSlot + 8u, game::kTaskSlot);
    const char name[] = "game_task.ovl";
    for (std::uint32_t i = 0; i < sizeof(name); ++i)
        memory.store8(game::kTaskSlot + 32u + i, static_cast<std::uint8_t>(name[i]));
}

struct Fixture {
    // The quest overlay and its data sit above 32 MiB, as on a PSP with 64.
    psprecomp::Runtime runtime{64u * 1024u * 1024u};
    psprecomp::AllegrexContext ctx{};
    Fixture() {
        mhp3rd::settings::current() = {};
        mhp3rd::settings::current().analog_camera = false;
        runtime.register_generated_unit(29u, 0x08878000u, 0x4000u, &original, nullptr);
        runtime.register_function(helper, &original, "recomp_unit_test");
        auto &memory = runtime.memory();
        for (const CodeWord &word : game_code_signature()) memory.store32(word.address, word.word);
        check(prepare_game_camera(runtime, &original), "the matching game code is accepted");
        memory.store32(camera_address + 0x70u, preset_address);
        memory.store16(camera_address + 0x80u, 0u);
        memory.store16(camera_address + 0x82u, 0u);
        memory.store8(camera_address + 0x91u, 0xFFu);
        write_float(memory, preset_address + 0x10u, 190.0f);
        write_float(memory, camera_address + 4u, 150.0f);
        // The hunter at the origin, the camera looking along -z (yaw 0).
        write_float(memory, camera_address + 0x10u, 0.0f);
        write_float(memory, camera_address + 0x14u, 190.0f);
        write_float(memory, camera_address + 0x18u, 0.0f);
        ctx.gpr[17] = camera_address;
        ctx.gpr[29] = stack_address;
        ctx.gpr[5] = stack_address + 0x50u;
        ctx.gpr[31] = return_pc;
        start_quest(memory);
        memory.store32(game::kMonsterTable, monster_a);
        memory.store32(game::kMonsterTable + 4u, companion);
        memory.store32(game::kMonsterTable + 8u, monster_b);
        place(memory, monster_a, 1000.0f, 0.0f);        // to the right, 90 degrees off the view
        place(memory, monster_b, 0.0f, -3000.0f);       // straight ahead, farther
        place(memory, companion, 0.0f, -100.0f, 50, 0); // no most health: not a large monster
    }
    void update() {
        write_float(runtime.memory(), stack_address + 0x34u, 150.0f);
        write_float(runtime.memory(), stack_address + 0x38u, 490.0f);
        check(runtime.invoke_isolated_aot(helper, ctx), "the rotation helper dispatches");
    }
    void frame(float stick_x = 0.0f) {
        set_rate(Source::Stick, stick_x, 0.0f);
        game_camera_frame(runtime);
        advance(1.0f / 30.0f, game_camera_degrees_per_second());
        update();
    }
    std::uint16_t yaw() { return runtime.memory().load16(camera_address + 0x82u); }
};

void test_geometry() {
    check(yaw_towards({0, 0, 0}, {0, 0, -100}) == 0u, "looking along -z is yaw 0");
    check(yaw_towards({0, 0, 0}, {100, 0, 0}) == 49152u, "looking along +x is three quarters of a turn");
    check(yaw_towards({0, 0, 0}, {0, 0, 100}) == 32768u, "looking along +z is half a turn");
    check(yaw_difference(65000u, 500u) == 1036, "differences go the short way round");
    check(yaw_difference(500u, 65000u) == -1036, "and both ways");
    std::uint16_t yaw = 0u;
    int largest = 0;
    for (int i = 0; i < 60; ++i) {
        const std::uint16_t next = lock_on_ease_yaw(yaw, 30000u);
        largest = std::max(largest, std::abs(yaw_difference(yaw, next)));
        yaw = next;
    }
    check(yaw == 30000u, "easing reaches the wanted yaw");
    check(largest <= 2048, "and never turns more than 11.25 degrees in one update");
    check(lock_on_ease_yaw(100u, 120u) == 120u, "a small rest is closed at once");
    check(lock_on_ease_yaw(1000u, 20000u, 0.0f) == 1000u, "with the monster on top of the hunter the yaw holds");
    check(yaw_difference(0u, lock_on_ease_yaw(0u, 32868u, 1.0f, 1)) > 0,
        "near half a turn, the turn keeps the way it was going");
    check(yaw_difference(0u, lock_on_ease_yaw(0u, 32868u)) < 0, "and otherwise takes the short way");
}

void test_monsters_and_pick() {
    Fixture f;
    auto &memory = f.runtime.memory();
    std::vector<LockMonster> found = lock_on_monsters(memory);
    check(found.size() == 2u, "a companion is no large monster");
    const Vec3 origin{0.0f, 190.0f, 0.0f};
    check(lock_on_pick(found, origin, 0u, std::nullopt, {}) == monster_b,
        "a tap picks the monster in view over a nearer one out of view");
    check(lock_on_pick(found, origin, 49152u, std::nullopt, {}) == monster_a, "or the one the camera looks at");
    check(lock_on_pick(found, origin, 32768u, std::nullopt, {}) == monster_a, "with none in view, the nearest");
    check(lock_on_pick(found, origin, 0u, monster_a, {monster_a}) == monster_b, "a second tap moves to the next one");
    check(!lock_on_pick(found, origin, 0u, monster_b, {monster_a, monster_b}), "after the last, a tap lets go");
    check(lock_on_pick(found, origin, 0u, monster_b, {monster_b}) == monster_a, "cycling wraps round the table");
    place(memory, monster_a, 1000.0f, 0.0f, 1000, 1000, true);
    check(lock_on_monsters(memory).size() == 1u, "a monster in another area cannot be locked");
    place(memory, monster_b, 0.0f, -3000.0f, 0, 1000);
    check(lock_on_monsters(memory).empty(), "nor a dead one");
    memory.store8(game::kTaskSlot + 32u, 'l');
    place(memory, monster_b, 0.0f, -3000.0f);
    check(lock_on_monsters(memory).empty(), "and nothing outside a quest");
}

void test_lock_follows_and_lets_go() {
    Fixture f;
    auto &memory = f.runtime.memory();
    f.frame();
    check(!game_camera_driving(), "nothing is driven before a tap, with the analog camera off");
    lock_on_tap();
    f.frame();
    check(lock_on_status().locked && lock_on_status().target == monster_b, "a tap locks onto the monster in view");
    check(game_camera_driving(), "the second stick is kept from the game while locked");
    // The monster walks round to the hunter's right; the camera follows.
    place(memory, monster_b, 3000.0f, 0.0f);
    for (int i = 0; i < 40; ++i) f.frame();
    check(std::abs(yaw_difference(f.yaw(), 49152u)) < 64, "the camera turns to keep the monster ahead");
    check(memory.load16(camera_address + 0x80u) == f.yaw(), "the game's target yaw is kept with it");
    // A push of the stick is the player taking the camera back.
    f.frame(1.0f);
    f.frame(1.0f);
    check(!lock_on_status().locked, "turning by hand lets go");
    f.frame();
    check(!game_camera_driving(), "and the stick goes back to the game");

    lock_on_tap();
    f.frame();
    check(lock_on_status().target == monster_a, "locked again, onto the nearer monster now in view");
    place(memory, monster_a, 1000.0f, 0.0f, 0, 1000);
    f.frame();
    check(!lock_on_status().locked, "a monster killed lets go");
    lock_on_tap();
    f.frame();
    check(lock_on_status().target == monster_b, "the next tap takes the one that is left");
    place(memory, monster_b, 3000.0f, 0.0f, 1000, 1000, true);
    f.frame();
    check(!lock_on_status().locked, "one that leaves the area lets go");
    place(memory, monster_a, 1000.0f, 0.0f, 1000, 1000, true);
    f.frame();
    check(!lock_on_status().locked, "one that leaves the area lets go");

    place(memory, monster_a, 1000.0f, 0.0f);
    lock_on_tap();
    f.frame();
    check(lock_on_status().locked, "locked once more");
    memory.store16(camera_address + 0x84u, 0x0080u); // the D-pad's turn
    f.frame();
    memory.store16(camera_address + 0x84u, 0u);
    check(!lock_on_status().locked, "a camera command of the game's lets go");

    lock_on_tap();
    f.frame();
    lock_on_tap();
    f.frame();
    check(!lock_on_status().locked, "with one monster a second tap lets go");
}

void test_off_changes_nothing() {
    Fixture f;
    mhp3rd::settings::current().lock_on = false;
    auto &memory = f.runtime.memory();
    const auto *p = memory.raw_pointer(camera_address, 0x100u);
    const std::vector<std::uint8_t> before(p, p + 0x100u);
    lock_on_tap();
    for (int i = 0; i < 5; ++i) f.frame();
    check(!lock_on_status().locked, "with the setting off a tap does nothing");
    const auto *q = memory.raw_pointer(camera_address, 0x100u);
    check(std::vector<std::uint8_t>(q, q + 0x100u) == before, "and the camera is left as the game made it");
}

void test_binds() {
    using namespace mhp3rd::input;
    TapDetector tap;
    check(!tap.update(true, false), "nothing while held");
    check(tap.update(false, false), "a tap when let go alone");
    check(!tap.update(true, false) && !tap.update(true, true) && !tap.update(false, false),
        "no tap when another input was pressed during the hold (R3 + Left)");
    check(!tap.update(true, true) && !tap.update(false, false), "nor when one was held as it went down (L3 + R3)");
    for (std::size_t i = 0; i < kPresets; ++i) {
        const Layout &l = layout(static_cast<Preset>(i));
        const Slots &pad_slots = l.pad[static_cast<std::size_t>(Action::LockOn)];
        check(!pad_slots[0].empty(), "every preset binds lock-on on the pad");
        check(!l.keys[static_cast<std::size_t>(Action::LockOn)][0].empty(), "and on the keyboard");
        for (std::size_t a = 0; a < kActions; ++a)
            check(conflicts(l.pad, static_cast<Action>(a)).empty(), "no preset has a conflict on the pad");
        for (std::size_t a = 0; a < kActions; ++a)
            check(conflicts(l.keys, static_cast<Action>(a)).empty(), "nor on the keyboard");
    }
    const Layout &d = layout(Preset::Default);
    check(d.pad[static_cast<std::size_t>(Action::LockOn)][0] == single(pad(PadInput::RightStick)),
        "R3 locks on in the default preset");
    check(acts_on_release(Action::LockOn) && !acts_on_release(Action::Screenshot), "only lock-on acts on release");
    check(
        layout(Preset::LeftHanded).pad[static_cast<std::size_t>(Action::LockOn)][0] == single(pad(PadInput::LeftStick)),
        "and L3 in the left-handed one");
    check(d.keys[static_cast<std::size_t>(Action::LockOn)][0] == single(mouse_button(2)) &&
            d.keys[static_cast<std::size_t>(Action::LockOn)][1] == single(from_name("T")),
        "the middle mouse button and T lock on from the keyboard");
    {
        // A tap's chord inside a longer one is no conflict either way; the
        // same chord inside a longer one for anything else still is.
        Bindings b{};
        const Chord r3 = single(pad(PadInput::RightStick));
        const Chord shot = chord(pad(PadInput::RightStick), pad(PadInput::DpadLeft));
        b[static_cast<std::size_t>(Action::LockOn)] = {r3};
        b[static_cast<std::size_t>(Action::Screenshot)] = {shot};
        check(conflicts(b, Action::LockOn).empty() && conflicts(b, Action::Screenshot).empty(),
            "R3 for lock-on beside R3 + D-pad left for a screenshot is no conflict");
        b[static_cast<std::size_t>(Action::HideHud)] = {r3};
        check(conflicts(b, Action::HideHud).size() == 2u, "R3 for Hide HUD clashes with lock-on and the screenshot");
    }
}

} // namespace

int main() {
    test_geometry();
    test_monsters_and_pick();
    test_lock_follows_and_lets_go();
    test_off_changes_nothing();
    test_binds();
    std::cout << (failures ? "FAIL" : "PASS") << ": lock-on (" << failures << " failures)\n";
    return failures ? 1 : 0;
}
