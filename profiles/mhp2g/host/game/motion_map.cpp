#include "game/motion_map.hpp"

#include "psprecomp/runtime.hpp"

#include <algorithm>
#include <cctype>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mhp2g::game {
namespace {

constexpr std::uint32_t kSetMotion = 0x088D85BCu;
constexpr std::uint32_t kHunter = 0x090AF180u;
// The hunter's fields this uses, read off recordings of the object in a quest (MODLOG): the motion playing now,
// the buttons pressed this frame (0x10 triangle, 0x20 circle), and the action byte the weapon's own logic looks
// at before it takes the next attack. Clearing that byte during a motion lets the next attack through.
constexpr std::uint32_t kMotionNow = 0x324u;
constexpr std::uint32_t kPressed = 0x481u;
constexpr std::uint32_t kAction = 0x299u;
constexpr std::uint32_t kQuestHunterClass = 0x089B6378u;
constexpr std::uint32_t kTriangle = 0x1000u; // SceCtrlButtons
constexpr std::uint32_t kCircle = 0x2000u;
constexpr std::uint32_t kCross = 0x4000u;
constexpr std::uint32_t kR = 0x0200u;
constexpr std::uint32_t kSquare = 0x8000u;
constexpr std::uint32_t kDrawnWalk = 1004u;
// The hunter's height: its position's Y and the copy the game's own falling works from. Written together, the
// game takes the hunter as in the air (it falls from there and lands by itself: motions 15 and 62).
constexpr std::uint32_t kHeight = 0x44u;
constexpr std::uint32_t kHeightBefore = 0x204u;
constexpr std::uint32_t kFallMotion = 15u;
constexpr std::uint32_t kLandMotion = 62u;

using Clock = std::chrono::steady_clock;

// The rules file (MHP2G_MOTION_MAP), read again whenever it changes:
//   <from> <to>                               one motion for another, always
//   start <y|b|yb> <motion>                   the attack a press starts when no rule's motion is playing
//   next <motion> <y|b|yb> <next> <seconds>   a press during <motion> leads to <next>, at the earliest <seconds> in
//   auto <motion> <next> <seconds>            <next> follows <motion> by itself after <seconds>
// y is triangle, b circle, yb both. The rules are a moveset taken from another game's recording (MODLOG): the
// weapon's own logic still decides that an attack starts; which motion it is, and when the next one may start,
// come from here.
struct State {
    MotionFunction original{};
    std::filesystem::path file;
    std::filesystem::file_time_type read_at{};
    std::unordered_map<std::uint32_t, std::uint32_t> map;
    // "drawn <from> <to>": as above, but only while a motion of the weapon's pack (1000 and up) is playing.
    std::unordered_map<std::uint32_t, std::uint32_t> drawn;
    std::unordered_map<std::string, std::uint32_t> start;
    std::map<std::pair<std::uint32_t, std::string>, std::pair<std::uint32_t, double>> next;
    std::unordered_map<std::uint32_t, std::pair<std::uint32_t, double>> autos;
    // The motion playing, and since when.
    std::uint32_t watched{};
    Clock::time_point since{};
    // Buttons pressed during the motion (bits of kPressed), and when the latest came.
    std::uint8_t buffered{};
    Clock::time_point buffered_at{};
    // The motion the next attack the game starts is to be, and the press that makes it start one.
    std::uint32_t pending{};
    Clock::time_point pending_at{};
    std::uint32_t inject{};
    unsigned inject_flips{};
    unsigned inject_wait{};
    // "guard <start> <start seconds> <hold> <hold seconds> <end>": while R is held with the weapon drawn, <start>
    // then <hold> (started again every <hold seconds>), and <end> when R is let go. "start ry|rb|ryb <motion>":
    // the moves of R with triangle, circle or both. The game is not shown R or those presses (motion_map_pad).
    struct Guard {
        std::uint32_t start{};
        double start_seconds{};
        std::uint32_t hold{};
        double hold_seconds{};
        std::uint32_t end{};
    };
    Guard guard;
    // "jump <motion> <second jump motion> <rise speed> <gravity> <hop speed> <landing motion>": R with cross, the
    // weapon drawn, throws the hunter up (units a second, units a second squared); an attack started in the air
    // lifts it again by the hop speed; one more jump is allowed in the air. "start ay|ab|ayb <motion>": the
    // attacks of triangle, circle and both in the air. The game's fall and landing motions are kept out.
    struct Jump {
        std::uint32_t motion{};
        std::uint32_t second{};
        float rise{};
        float gravity{};
        float hop{};
        std::uint32_t landing{};
    };
    Jump jump;
    // "mode <name> <motion> ...": starting one of these motions puts the weapon in that mode (the Tonfa's long
    // and short). A start or next rule whose buttons end in @<name> ("next 1436 b@long 1442 0.6") holds only in
    // that mode and goes before the plain one.
    std::unordered_map<std::uint32_t, std::string> mode_of;
    // "dive <motions...>": attacks in the air that come down and land by themselves: no lift at their start and
    // no landing motion at the ground.
    std::set<std::uint32_t> dives;
    std::unordered_map<std::uint32_t, std::uint32_t> rounds; // motion -> highest "#n" its next rules name
    std::unordered_map<std::uint32_t, std::uint32_t> played; // motion -> times it has played in the current chain
    std::string mode;
    std::string note;
    // "rehit 1": a motion a rule starts gets the weapon logic's attack step set back to its start (hunter +0x1D5 = 1,
    // +0x1D6 = 0), so that its own code makes the hit again for the new motion.
    bool rehit{};
    // "hit <motion> <seconds> <attack> [other hand: 1]": that long into the motion, the game's attack <attack> is
    // made (its own function 0x09B5E0F0, which every weapon's code calls with an attack number at its hit
    // frames; the number picks a line of the game's attack table: delay, length, power, which hand's weapon).
    // Several lines for one motion make several hits. With any hit line in the file, the attacks the game's own
    // weapon logic makes for the hunter in weapon motions are dropped: that logic still runs underneath the
    // rule-started motions and would hit at the Dual Blades' times.
    // "attack <name> <number> <shape> <power> <kind> <stun>": an attack of the rules' own (number: one whose hit
    // starts late in the game's table, such as 48), usable as
    // "hit <motion> <seconds> <name>". It is the game's attack <number> with these values of its table line put in
    // its place: shape (which bone the hit follows: 9 and 10 the two hands' weapons, 1 and 2 the feet), power,
    // kind (1 blunt, 2 cutting) and stun.
    struct Attack {
        std::uint32_t number{};
        std::uint32_t shape{};
        std::uint32_t power{};
        std::uint32_t kind{};
        std::uint32_t stun{};
        std::uint32_t waited{}; // flips a made record has waited for its first turn
        bool own{};
    };
    std::unordered_map<std::string, Attack> attacks;
    struct Hit {
        double seconds{};
        Attack attack;
        bool done{};
    };
    std::unordered_map<std::uint32_t, std::vector<Hit>> hits;
    std::unordered_map<std::uint32_t, Attack> made; // attack records made for hits of the rules' own attacks
    psprecomp::Runtime::RecompiledFunction make_attack{};
    bool making{};
    bool airborne{};
    // "forms <bones per weapon> <seconds>" and "form <mode> <bone> <x> <y> <z> <turn about x, degrees>": the shape
    // of the weapon in each mode ("away" = sheathed): where its bones sit, set in the weapon's skeleton in
    // memory and moved there over <seconds> when the mode changes. A paired weapon's second half has the same
    // bones <bones per weapon> further on.
    struct FormBone {
        float x{}, y{}, z{}, turn{};
    };
    std::unordered_map<std::string, std::unordered_map<std::uint32_t, FormBone>> forms;
    std::unordered_map<std::uint32_t, FormBone> shown;
    std::uint32_t form_step{};
    float form_seconds{0.3f};
    Clock::time_point form_at{};
    // "dash <motion> <speed>": pushing the stick twice quickly with the weapon drawn turns the drawn walk (1004)
    // into this motion, the hunter covering <speed> times the walk's ground, until the stick is let go.
    // "start dy|db ..." are the attacks out of it.
    struct Dash {
        std::uint32_t motion{};
        float speed{1.0f};
    } dash;
    bool dash_wanted{};
    bool stick_pushed{};
    float stick_x{}, stick_y{};
    Clock::time_point push_at{}, release_at{};
    bool dash_from{}; // a position of the last flip is held
    float dash_x{}, dash_z{};
    bool unstick{}; // landed: the game's falling action is to be ended at the next rest
    bool second_used{};
    float ground{};
    float height{};
    float speed{};
    Clock::time_point air_at{};
    Clock::time_point landed_at{};
    // The pad as it is, at the last read and at the flip before.
    std::uint32_t real{};
    std::uint32_t real_before{};
    // The engine's own call of the game's set-motion is under way: the wrapper lets it through as it is.
    bool direct{};
    bool trace{};
};
State state;

bool has_rules() {
    return !state.start.empty() || !state.next.empty() || !state.autos.empty();
}

bool rule_motion(std::uint32_t motion) {
    if (state.autos.count(motion) != 0u) return true;
    const auto first = state.next.lower_bound({motion, std::string()});
    return first != state.next.end() && first->first.first == motion;
}

// Rule names of the hunter's pressed bits: triangle "y", circle "b", both "yb"; alone, square "s" and cross "c"
// (those two only come from presses in the air).
std::string button_name(std::uint8_t pressed) {
    return (pressed & 0x30u) == 0x30u ? "yb" : (pressed & 0x20u) != 0u ? "b" : (pressed & 0x10u) != 0u ? "y"
        : (pressed & 0x80u) != 0u ? "s" : (pressed & 0x40u) != 0u ? "c" : "";
}

void reload() {
    std::error_code ec;
    const auto changed = std::filesystem::last_write_time(state.file, ec);
    if (ec || changed == state.read_at) return;
    state.read_at = changed;
    state.map.clear();
    state.guard = {};
    state.jump = {};
    state.mode_of.clear();
    state.hits.clear();
    state.attacks.clear();
    state.rounds.clear();
    state.played.clear();
    state.dives.clear();
    state.dash = {};
    state.forms.clear();
    state.shown.clear();
    state.form_step = 0u;
    state.rehit = false;
    state.drawn.clear();
    state.start.clear();
    state.next.clear();
    state.autos.clear();
    std::ifstream in(state.file);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string first;
        if (!(words >> first) || first[0] == '#') continue;
        if (first == "start") {
            std::string button;
            std::uint32_t motion = 0u;
            if (words >> button >> motion) state.start[button] = motion;
        } else if (first == "next") {
            std::uint32_t motion = 0u;
            std::uint32_t to = 0u;
            std::string button;
            double seconds = 0.0;
            if (words >> motion >> button >> to >> seconds) {
                state.next[{motion, button}] = {to, seconds};
                // "y#2", "b@short#2": the rule for the 2nd time the motion plays within one chain
                if (const auto mark = button.rfind('#'); mark != std::string::npos) {
                    const auto nth = static_cast<std::uint32_t>(std::strtoul(button.c_str() + mark + 1u, nullptr, 10));
                    state.rounds[motion] = (std::max)(state.rounds[motion], nth);
                }
            }
        } else if (first == "hit") {
            std::uint32_t motion = 0u;
            State::Hit hit;
            std::string what;
            if (words >> motion >> hit.seconds >> what) {
                if (const auto own = state.attacks.find(what); own != state.attacks.end()) {
                    hit.attack = own->second;
                    state.hits[motion].push_back(hit);
                } else if (!what.empty() && what.find_first_not_of("0123456789") == std::string::npos) {
                    hit.attack.number = static_cast<std::uint32_t>(std::stoul(what));
                    state.hits[motion].push_back(hit);
                }
            }
        } else if (first == "attack") {
            std::string name;
            State::Attack attack;
            attack.own = true;
            if (words >> name >> attack.number >> attack.shape >> attack.power >> attack.kind >> attack.stun)
                state.attacks[name] = attack;
        } else if (first == "rehit") {
            int on = 0;
            words >> on;
            state.rehit = on != 0;
        } else if (first == "mode") {
            std::string name;
            std::uint32_t motion = 0u;
            if (words >> name) {
                if (state.mode.empty()) state.mode = name; // the first mode named is the one to start in
                while (words >> motion) state.mode_of[motion] = name;
            }
        } else if (first == "jump") {
            State::Jump j;
            if (words >> j.motion >> j.second >> j.rise >> j.gravity >> j.hop >> j.landing) state.jump = j;
        } else if (first == "forms") {
            words >> state.form_step >> state.form_seconds;
        } else if (first == "form") {
            std::string mode;
            std::uint32_t bone = 0u;
            State::FormBone f;
            if (words >> mode >> bone >> f.x >> f.y >> f.z >> f.turn) {
                f.turn *= 3.14159265f / 180.0f;
                state.forms[mode][bone] = f;
            }
        } else if (first == "dash") {
            State::Dash d;
            if (words >> d.motion >> d.speed) state.dash = d;
        } else if (first == "dive") {
            std::uint32_t motion = 0u;
            while (words >> motion) state.dives.insert(motion);
        } else if (first == "guard") {
            State::Guard g;
            if (words >> g.start >> g.start_seconds >> g.hold >> g.hold_seconds >> g.end) state.guard = g;
        } else if (first == "drawn") {
            std::uint32_t from = 0u;
            std::uint32_t to = 0u;
            if (words >> from >> to) state.drawn[from] = to;
        } else if (first == "auto") {
            std::uint32_t motion = 0u;
            std::uint32_t to = 0u;
            double seconds = 0.0;
            if (words >> motion >> to >> seconds) state.autos[motion] = {to, seconds};
        } else {
            std::uint32_t to = 0u;
            if (words >> to) state.map[static_cast<std::uint32_t>(std::strtoul(first.c_str(), nullptr, 10))] = to;
        }
    }
    std::cout << "[motion] " << state.map.size() << " replacement(s), " << state.start.size() << " start(s), "
              << state.next.size() << " next rule(s), " << state.autos.size() << " auto rule(s) read" << std::endl;
}

constexpr std::uint32_t kMakeAttack = 0x09B5E0F0u;  // game_task.ovl: (owner, creature, attack number, argument)
constexpr std::uint32_t kAttackOwner = 0x09C14250u; // holds the first argument the weapons' code passes

// In place of the game's attack maker: the hunter's own attacks in weapon motions are left out while hit rules
// are loaded (see State::Hit); everything else, and the hits the rules make, goes through.
void make_attack(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    if (ctx.pc == kMakeAttack && !state.making && !state.hits.empty() && ctx.gpr[5] == kHunter &&
        runtime.memory().load16(kHunter + kMotionNow) >= 1000u) {
        if (state.trace)
            std::cout << "[motion] left out the game's own attack " << ctx.gpr[6] << " in "
                      << runtime.memory().load16(kHunter + kMotionNow) << std::endl;
        ctx.pc = ctx.gpr[31];
        return;
    }
    state.make_attack(runtime, ctx);
}

// The rules' own attacks: the game copies an attack's table line (24 bytes) into its record (+28) on the record's
// first turn and marks it (+20). Right after that turn the values of the rules' attack go over the copy. The
// attacks are built on a number whose hit starts some frames late, so nothing has been hit with the table's
// values by then; the wait is cut off here, so the hit is on from the next frame, for the line's length
// (4 frames with number 48): from about 0.08 s to 0.21 s after the rule's time.
void finish_attacks(psprecomp::GuestMemory &memory) {
    for (auto made = state.made.begin(); made != state.made.end();) {
        const std::uint32_t record = made->first;
        const State::Attack &attack = made->second;
        const bool ours = memory.raw_pointer(record, 224u) != nullptr && memory.load32(record + 16u) == kHunter &&
            memory.load8(record + 13u) == attack.number;
        if (ours && memory.load8(record + 20u) != 1u && ++made->second.waited < 8u) {
            ++made;
            continue;
        }
        if (ours) {
            constexpr std::uint32_t kShapes = 0x09BF7118u; // pointers to the hit shapes
            memory.store8(record + 28u + 0u, 0u);
            memory.store8(record + 28u + 2u, static_cast<std::uint8_t>(attack.power));
            memory.store8(record + 28u + 9u, static_cast<std::uint8_t>(attack.kind));
            memory.store8(record + 28u + 10u, static_cast<std::uint8_t>(attack.shape));
            memory.store32(record + 52u, memory.load32(kShapes + 4u * attack.shape));
            memory.store8(record + 28u + 20u, static_cast<std::uint8_t>(attack.stun));
            if (attack.kind == 1u) memory.store8(record + 28u + 16u, 3u); // the blunt weapons' hit mark and sound
        }
        made = state.made.erase(made);
    }
}

// The weapon's shape for the mode (see State::FormBone). Weapon bone k: 0x250 bytes each from kWeaponBones; its
// local translation is kept three times (+0, +0x48, +0x70) and its rotation (x, y, z, radians) twice (-0x0C,
// +0x64). A bone is only written while it holds what these rules or the weapon's file put there (y and z of
// the form), so another weapon's skeleton is left alone.
void apply_forms(psprecomp::GuestMemory &memory, std::uint32_t motion, Clock::time_point now) {
    constexpr std::uint32_t kWeaponBones = kHunter + 0x9658u;
    if (state.forms.empty()) return;
    auto form = state.forms.find(motion < 1000u ? "away" : state.mode);
    if (form == state.forms.end()) return;
    const float dt = std::clamp(std::chrono::duration<float>(now - state.form_at).count(), 0.0f, 0.1f);
    state.form_at = now;
    const float step = state.form_seconds > 0.0f ? dt / state.form_seconds : 1.0f;
    for (const auto &[bone, want] : form->second) {
        auto [at, fresh] = state.shown.try_emplace(bone, want);
        State::FormBone &now_at = at->second;
        if (!fresh) {
            // towards the form, each value at the pace that takes the whole way in form_seconds
            const auto towards = [&](float &value, float target, float span) {
                const float move = std::max(span, 1.0f) * step;
                value = std::abs(target - value) <= move ? target : value + (target > value ? move : -move);
            };
            towards(now_at.x, want.x, 60.0f);
            towards(now_at.turn, want.turn, 1.6f);
        }
        for (std::uint32_t half = 0u; half < 2u; ++half) {
            if (half == 1u && state.form_step == 0u) break;
            const std::uint32_t base = kWeaponBones + 0x250u * (bone + half * state.form_step);
            if (memory.raw_pointer(base - 0x20u, 0xA0u) == nullptr) continue;
            const float y = std::bit_cast<float>(memory.load32(base + 4u));
            const float z = std::bit_cast<float>(memory.load32(base + 8u));
            if (std::abs(y - want.y) > 0.01f || std::abs(z - want.z) > 0.01f) continue;
            for (const std::uint32_t copy : {0x00u, 0x48u, 0x70u})
                memory.store32(base + copy, std::bit_cast<std::uint32_t>(now_at.x));
            memory.store32(base - 0x0Cu, std::bit_cast<std::uint32_t>(now_at.turn));
            memory.store32(base + 0x64u, std::bit_cast<std::uint32_t>(now_at.turn));
        }
    }
}

// The attack maker lives in an overlay that comes and goes: (re)place it whenever the game's own is there.
void watch_attack_maker(psprecomp::Runtime &runtime) {
    const auto &memory = runtime.memory();
    if (const auto now = runtime.function_at(kMakeAttack);
        now != nullptr && now != &make_attack && memory.raw_pointer(kMakeAttack, 8u) != nullptr &&
        memory.load32(kMakeAttack) == 0x27BDFFE0u && memory.load32(kMakeAttack + 4u) == 0xAFBF001Cu) {
        state.make_attack = now;
        state.made.clear();
        runtime.register_function(kMakeAttack, &make_attack, "mhp2g_make_attack");
    }
}

// Calls a function of the game on a copy of the flipping thread's context, the stack moved down: runs until the
// function returns to the stop mark. False when it did not come back.
bool call_game(psprecomp::Runtime &runtime, const psprecomp::AllegrexContext &thread, std::uint32_t function,
    std::uint32_t a0, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3) {
    constexpr std::uint32_t kReturn = 0x08000000u; // no code there
    psprecomp::AllegrexContext ctx = thread;
    ctx.gpr[4] = a0;
    ctx.gpr[5] = a1;
    ctx.gpr[6] = a2;
    ctx.gpr[7] = a3;
    ctx.gpr[29] -= 0x400u;
    ctx.gpr[31] = kReturn;
    ctx.pc = function;
    unsigned steps = 0u;
    while (ctx.pc != kReturn && steps++ < 20000u)
        if (!runtime.invoke_isolated_aot(ctx.pc, ctx)) break;
    return ctx.pc == kReturn;
}

// The start and next rules for a button, the current mode's first.
auto find_start(const std::string &button) {
    if (!state.mode.empty())
        if (const auto found = state.start.find(button + "@" + state.mode); found != state.start.end()) return found;
    return state.start.find(button);
}

// The next rule for a button in a motion: the rule for this time round first ("#2": the motion plays for the
// 2nd time in the chain; the count goes round after the highest number the rules name), the current mode's first.
auto find_next(std::uint32_t motion, const std::string &button) {
    std::string round;
    if (const auto rounds = state.rounds.find(motion); rounds != state.rounds.end() && rounds->second > 1u) {
        const auto played = state.played.find(motion);
        const std::uint32_t nth = played == state.played.end() || played->second == 0u
            ? 1u : (played->second - 1u) % rounds->second + 1u;
        if (nth > 1u) round = "#" + std::to_string(nth);
    }
    for (const std::string &tail : {round, std::string()}) {
        if (!state.mode.empty())
            if (const auto found = state.next.find({motion, button + "@" + state.mode + tail});
                found != state.next.end())
                return found;
        if (const auto found = state.next.find({motion, button + tail}); found != state.next.end()) return found;
        if (round.empty()) break;
    }
    return state.next.end();
}

void set_motion(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    // Only at the function's entry, and only for the player's hunter: the unit is entered at
    // other addresses too, and monsters and companions start motions through the same code.
    if (state.direct) {
        state.original(runtime, ctx);
        return;
    }
    if (ctx.pc == kSetMotion && ctx.gpr[4] == kHunter) {
        reload();
        const std::uint32_t asked = ctx.gpr[5];
        const auto found = state.map.find(asked);
        if (found != state.map.end()) ctx.gpr[5] = found->second;
        if (state.dash_wanted && state.dash.motion != 0u && asked == kDrawnWalk) ctx.gpr[5] = state.dash.motion;
        if (const auto drawn = state.drawn.find(asked);
            drawn != state.drawn.end() && runtime.memory().load16(kHunter + kMotionNow) >= 1000u)
            ctx.gpr[5] = drawn->second;
        std::string note;
        if (state.jump.motion != 0u) {
            const bool just_landed = std::chrono::duration<double>(Clock::now() - state.landed_at).count() < 0.6;
            // During a jump of the rules the game's own movement motions stay out: its fall and landings, a roll
            // from a press that slipped through. Just after it, its fall and landing. A motion from 100 up is
            // something done to the hunter (a hit): the jump is over, and the game has it.
            if ((state.airborne && asked < 100u) || (just_landed && (asked == kFallMotion || asked == kLandMotion || asked == 19u /* landing from high up */))) {
                if (state.trace) std::cout << "[motion] " << asked << " kept out (jump)" << std::endl;
                ctx.pc = ctx.gpr[31];
                return;
            }
            if (state.airborne && asked >= 100u && asked < 1000u) state.airborne = false;
        }
        if (asked >= 1400u && asked < 1500u && has_rules()) {
            auto &memory = runtime.memory();
            const std::uint32_t now = memory.load16(kHunter + kMotionNow);
            if (state.pending != 0u) {
                ctx.gpr[5] = state.pending;
                note = " [rule]";
                state.pending = 0u;
                state.inject = 0u;
                state.inject_flips = 0u;
            } else {
                // The weapon's own logic started an attack by itself: from rest, the start of a chain; during one of the
                // rules' motions (its own combo window opened first), the rule for the button, whatever its time.
                std::string button = button_name(memory.load8(kHunter + kPressed) & 0x30u);
                if (button.empty()) button = button_name(state.buffered & 0x30u);
                if (button.empty()) button = asked % 100u >= 10u ? "b" : "y";
                std::uint32_t to = 0u;
                if (rule_motion(now)) {
                    // During one of the rules' motions the rules decide, not the weapon's own combo windows:
                    // the press is kept for motion_map_frame(), which starts the next motion at its time, and
                    // this start does not happen (the function returns to its caller at once).
                    const std::uint8_t bits = button == "yb" ? 0x30u : button == "b" ? 0x20u : 0x10u;
                    if (std::chrono::duration<double>(Clock::now() - state.buffered_at).count() > 0.12)
                        state.buffered = 0u;
                    state.buffered |= bits;
                    state.buffered_at = Clock::now();
                    if (state.trace) std::cout << "[motion] " << asked << " held back [" << button << "]" << std::endl;
                    ctx.pc = ctx.gpr[31];
                    return;
                }
                if (to == 0u) {
                    auto first = state.start.end();
                    if (state.dash.motion != 0u && now == state.dash.motion) first = find_start("d" + button);
                    if (first == state.start.end()) first = find_start(button);
                    if (first == state.start.end()) first = find_start("y");
                    if (first != state.start.end()) to = first->second;
                }
                if (to != 0u) {
                    ctx.gpr[5] = to;
                    note = " [" + button + "]";
                }
            }
        }
        if (state.trace)
            std::cout << "[motion] " << asked << (ctx.gpr[5] != asked ? " -> " + std::to_string(ctx.gpr[5]) : "") << note
                      << " (a2 " << ctx.gpr[6] << ", a3 " << ctx.gpr[7] << ")" << std::endl;
    }
    state.original(runtime, ctx);
}

// Starts `motion` for the hunter by calling the game's own set-motion (a0 hunter, a1 motion, a2 2, a3 0, as its
// attack code does), on a copy of the flipping thread's context with the stack moved down. The hunter stays in
// the attack it is in: only the motion changes, and the game ends the attack when this motion ends.
void trigger(psprecomp::Runtime &runtime, const psprecomp::AllegrexContext &thread, std::uint32_t motion) {
    constexpr std::uint32_t kReturn = 0x08000000u; // no code there: the call is over when the pc gets here
    psprecomp::AllegrexContext ctx = thread;
    ctx.gpr[4] = kHunter;
    ctx.gpr[5] = motion;
    ctx.gpr[6] = 2u;
    ctx.gpr[7] = 0u;
    ctx.gpr[29] -= 0x200u;
    ctx.gpr[31] = kReturn;
    ctx.pc = kSetMotion;
    state.direct = true;
    unsigned steps = 0u;
    while (ctx.pc != kReturn && steps++ < 4096u)
        if (!runtime.invoke_isolated_aot(ctx.pc, ctx)) break;
    state.direct = false;
    if (const auto mode = state.mode_of.find(motion); mode != state.mode_of.end()) {
        if (state.mode != mode->second) {
            state.note = mode->second + " mode";
            state.note[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(state.note[0])));
        }
        state.mode = mode->second;
    }
    if (state.rehit && motion >= 1400u && motion < 1500u && !state.airborne) {
        runtime.memory().store8(kHunter + 0x1D5u, 1u);
        runtime.memory().store8(kHunter + 0x1D6u, 0u);
    }
    if (state.airborne && motion >= 1400u && state.dives.count(motion) == 0u)
        state.speed = std::max(state.speed, state.jump.hop);
    if (state.trace)
        std::cout << "[motion] rule: " << state.watched << " -> " << motion << (ctx.pc == kReturn ? "" : " (call did not return)")
                  << " in " << steps << " step(s)" << std::endl;
}

} // namespace

void prepare_motion_map(psprecomp::Runtime &runtime, MotionFunction original) {
    const char *file = std::getenv("MHP2G_MOTION_MAP");
    if (file == nullptr || *file == '\0' || !original || !runtime.has_function(kSetMotion)) return;
    state.original = original;
    state.file = file;
    state.trace = std::getenv("MHP2G_TRACE_MOTION") != nullptr;
    runtime.register_function(kSetMotion, &set_motion, "mhp2g_set_motion");
    std::cout << "[motion] replacing motions from " << file << std::endl;
}

void motion_map_frame(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    if (state.inject_flips > 0u && --state.inject_flips == 0u) state.inject = 0u;
    if (!state.original || !has_rules()) return;
    auto &memory = runtime.memory();
    if (state.pending != 0u) {
        memory.store8(kHunter + kAction, 0u);
        if (state.inject_wait > 0u && --state.inject_wait == 0u) {
            state.inject = kTriangle;
            state.inject_flips = 3u;
        }
    }
    if (memory.raw_pointer(kHunter, 0x500u) == nullptr || memory.load32(kHunter) != kQuestHunterClass) return;
    const std::uint32_t motion = memory.load16(kHunter + kMotionNow);
    const auto now = Clock::now();
    const std::uint32_t pressed_now = state.real & ~state.real_before;
    state.real_before = state.real;
    if (motion != state.watched) {
        if (state.trace)
            std::cout << "[motion] now " << motion << (rule_motion(motion) ? " (rules)" : "") << " after "
                      << std::chrono::duration<double>(now - state.since).count() << " s, action "
                      << static_cast<unsigned>(memory.load8(kHunter + kAction)) << "/"
                      << static_cast<unsigned>(memory.load8(kHunter + 0x288u)) << "/"
                      << static_cast<unsigned>(memory.load8(kHunter + 0x1D5u)) << std::endl;
        state.watched = motion;
        state.since = now;
        state.buffered = 0u;
        if (rule_motion(motion)) {
            ++state.played[motion];
        } else {
            state.played.clear(); // out of the chain: at rest, hit, rolling
            // Back at rest after a landing: the game still has the hunter in its own falling action (its fall and
            // landing motions were kept out, so it never ends) and takes no steps for some 3 seconds. End it.
            if (state.unstick && memory.load8(kHunter + kAction) == 9u) memory.store8(kHunter + kAction, 0u);
            state.unstick = false;
        }
        if (const auto hits = state.hits.find(motion); hits != state.hits.end())
            for (State::Hit &hit : hits->second) hit.done = false;
    }
    // A planned motion the game has not started within half a second is off (hit, rolled, sheathed). The game
    // may pass through its rest motion on the way, so a change of motion alone does not call it off.
    if (state.pending != 0u && std::chrono::duration<double>(now - state.pending_at).count() > 0.5) {
        state.pending = 0u;
        state.inject = 0u;
        state.inject_flips = 0u;
    }
    const double into = std::chrono::duration<double>(now - state.since).count();
    if (state.dash.motion != 0u) {
        if (state.dash_wanted && !state.airborne && motion == kDrawnWalk) {
            state.dash_from = false;
            trigger(runtime, ctx, state.dash.motion);
            return;
        }
        if (motion == state.dash.motion && state.dash_wanted) {
            // further than the walk carries: the step of this frame, stretched
            float x = std::bit_cast<float>(memory.load32(kHunter + 0x40u));
            float z = std::bit_cast<float>(memory.load32(kHunter + 0x48u));
            if (state.dash_from) {
                const float dx = x - state.dash_x;
                const float dz = z - state.dash_z;
                if (dx * dx + dz * dz < 100.0f * 100.0f) {
                    x += dx * (state.dash.speed - 1.0f);
                    z += dz * (state.dash.speed - 1.0f);
                    for (const std::uint32_t copy : {0x40u, 0x200u, 0x210u}) {
                        memory.store32(kHunter + copy, std::bit_cast<std::uint32_t>(x));
                        memory.store32(kHunter + copy + 8u, std::bit_cast<std::uint32_t>(z));
                    }
                }
            }
            state.dash_from = true;
            state.dash_x = x;
            state.dash_z = z;
        } else {
            state.dash_from = false;
        }
    }
    if (!state.hits.empty()) watch_attack_maker(runtime);
    finish_attacks(memory);
    apply_forms(memory, motion, now);
    if (const auto hits = state.hits.find(motion); hits != state.hits.end()) {
        for (State::Hit &hit : hits->second) {
            if (hit.done || into < hit.seconds) continue;
            hit.done = true;
            state.making = true;
            const std::uint32_t owner = memory.load32(kAttackOwner);
            const bool back = runtime.has_function(kMakeAttack) &&
                call_game(runtime, ctx, kMakeAttack, owner, kHunter, hit.attack.number, 0u);
            state.making = false;
            // the new record is the owner's newest (+20528): its table line is copied on its first turn
            if (const std::uint32_t record = back ? memory.load32(owner + 20528u) : 0u;
                hit.attack.own && record != 0u && memory.raw_pointer(record, 224u) != nullptr &&
                memory.load32(record + 16u) == kHunter && memory.load8(record + 13u) == hit.attack.number) {
                if (state.made.size() > 64u) state.made.clear();
                state.made[record] = hit.attack;
            }
            if (state.trace)
                std::cout << "[motion] hit: attack " << hit.attack.number << " at " << into << " s into " << motion
                          << (back ? "" : " (call did not return)") << std::endl;
        }
    }
    if (state.jump.motion != 0u && motion >= 1000u) {
        const State::Jump &j = state.jump;
        const bool r = (state.real & kR) != 0u;
        const bool jump_pressed = r && (pressed_now & kCross) != 0u;
        if (!state.airborne && jump_pressed && (motion < 1400u || rule_motion(motion))) {
            state.airborne = true;
            state.second_used = false;
            state.ground = std::bit_cast<float>(memory.load32(kHunter + kHeight));
            state.height = state.ground;
            state.speed = j.rise;
            state.air_at = now;
            state.buffered = 0u;
            trigger(runtime, ctx, j.motion);
        } else if (state.airborne) {
            const float dt = std::clamp(std::chrono::duration<float>(now - state.air_at).count(), 0.0f, 0.05f);
            state.air_at = now;
            if (jump_pressed && !state.second_used) {
                state.second_used = true;
                state.speed = j.rise * 0.8f;
                state.buffered = 0u;
                trigger(runtime, ctx, j.second);
            }
            state.speed -= j.gravity * dt;
            state.height += state.speed * dt;
            if (state.height <= state.ground && state.speed < 0.0f) {
                // Down: the ground takes the hunter back, with the rules' landing.
                state.airborne = false;
                state.landed_at = now;
                state.height = state.ground;
                state.buffered = 0u;
                state.unstick = true;
                if (state.dives.count(motion) == 0u) trigger(runtime, ctx, j.landing);
            }
            const std::uint32_t y = std::bit_cast<std::uint32_t>(state.height);
            memory.store32(kHunter + kHeight, y);
            memory.store32(kHunter + kHeightBefore, y);
            if (!state.airborne) return;
            // An attack in the air: the rule for the motion playing, at its time (below), or else the first
            // attack of the button in the air, at once.
            if ((pressed_now & (kTriangle | kCircle)) != 0u) {
                const bool y_held = (state.real & kTriangle) != 0u;
                const bool b_held = (state.real & kCircle) != 0u;
                const std::string button = y_held && b_held ? "yb" : y_held ? "y" : "b";
                if (r) {
                    // With R in the air: "start ary|arb" (the changes of mode in the air), at once.
                    if (const auto first = find_start("ar" + button); first != state.start.end()) {
                        trigger(runtime, ctx, first->second);
                        return;
                    }
                } else if (find_next(motion, button) != state.next.end()) {
                    state.buffered = y_held && b_held ? 0x30u : y_held ? 0x10u : 0x20u;
                    state.buffered_at = now;
                } else if (const auto first = find_start("a" + button); first != state.start.end()) {
                    trigger(runtime, ctx, first->second);
                    return;
                }
            } else if ((pressed_now & kSquare) != 0u || ((pressed_now & kCross) != 0u && !r)) {
                // Square ("s") and cross without R ("c") in the air, the same way.
                const bool square = (pressed_now & kSquare) != 0u;
                const std::string button = square ? "s" : "c";
                if (find_next(motion, button) != state.next.end()) {
                    state.buffered = square ? 0x80u : 0x40u;
                    state.buffered_at = now;
                } else if (const auto first = find_start("a" + button); first != state.start.end()) {
                    trigger(runtime, ctx, first->second);
                    return;
                }
            }
        }
    }
    if (state.guard.start != 0u && motion >= 1000u && !state.airborne) {
        const State::Guard &g = state.guard;
        const bool r = (state.real & kR) != 0u;
        const bool rest = motion == 1000u || motion == 1001u;
        const bool guarding = motion == g.start || motion == g.hold;
        if (r && (rest || guarding) && (pressed_now & (kTriangle | kCircle)) != 0u) {
            const bool y = (state.real & kTriangle) != 0u;
            const bool b = (state.real & kCircle) != 0u;
            const auto move = find_start(y && b ? "ryb" : y ? "ry" : "rb");
            if (move != state.start.end()) {
                trigger(runtime, ctx, move->second);
                return;
            }
        }
        if (r && rest) {
            trigger(runtime, ctx, g.start);
            return;
        }
        if (motion == g.start && into >= g.start_seconds) {
            trigger(runtime, ctx, r ? g.hold : g.end);
            return;
        }
        if (motion == g.hold && (!r || into >= g.hold_seconds)) {
            trigger(runtime, ctx, r ? g.hold : g.end);
            return;
        }
    }
    if (!rule_motion(motion) || state.pending != 0u) return;
    // Presses after the first moments (those belong to the press that started this motion). Two buttons a few
    // frames apart count as pressed together.
    const std::uint8_t pressed = memory.load8(kHunter + kPressed) & 0x30u;
    if (pressed != 0u && into > 0.15) {
        if (std::chrono::duration<double>(now - state.buffered_at).count() > 0.12) state.buffered = 0u;
        state.buffered |= pressed;
        state.buffered_at = now;
    }
    if (state.buffered != 0u) {
        const std::string button = button_name(state.buffered);
        auto rule = find_next(motion, button);
        // Both buttons with no rule of their own: the last one pressed.
        if (rule == state.next.end() && button == "yb") rule = find_next(motion, button_name(pressed));
        if (rule != state.next.end() && into >= rule->second.second) {
            state.buffered = 0u;
            trigger(runtime, ctx, rule->second.first);
            return;
        }
    }
    if (const auto rule = state.autos.find(motion); rule != state.autos.end() && into >= rule->second.second)
        trigger(runtime, ctx, rule->second.first);
}

unsigned motion_map_pad(unsigned buttons) {
    state.real = buttons;
    if (state.original && state.guard.start != 0u && state.watched >= 1000u && (buttons & kR) != 0u)
        buttons &= ~(kR | kTriangle | kCircle | kCross);
    // In the air, and for a moment after landing, the attack and dodge buttons are the rules' alone.
    if (state.airborne || std::chrono::duration<double>(Clock::now() - state.landed_at).count() < 0.3)
        buttons &= ~(kR | kTriangle | kCircle | kCross | kSquare);
    return buttons;
}

void motion_map_stick(std::uint8_t analog_x, std::uint8_t analog_y) {
    if (state.dash.motion == 0u) return;
    const float x = (static_cast<float>(analog_x) - 128.0f) / 127.0f;
    const float y = (static_cast<float>(analog_y) - 128.0f) / 127.0f;
    const float size = std::sqrt(x * x + y * y);
    const auto now = Clock::now();
    if (!state.stick_pushed && size > 0.7f) {
        // pushed, let go and pushed again the same way within a moment: the dash
        const double pause = std::chrono::duration<double>(now - state.release_at).count();
        const double before = std::chrono::duration<double>(state.release_at - state.push_at).count();
        if (pause < 0.4 && before > 0.0 && before < 0.5 && x * state.stick_x + y * state.stick_y > 0.5f * size)
            state.dash_wanted = true;
        state.stick_pushed = true;
        state.push_at = now;
        state.stick_x = x / size;
        state.stick_y = y / size;
    } else if (state.stick_pushed && size < 0.35f) {
        state.stick_pushed = false;
        state.dash_wanted = false;
        state.release_at = now;
    }
}

std::string motion_map_take_note() {
    return std::exchange(state.note, std::string());
}

unsigned motion_map_buttons() {
    return state.inject;
}

} // namespace mhp2g::game
