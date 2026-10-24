#include "kernel/fast_forward.hpp"

#include "adhoc/session.hpp"
#include "hle/hle_common.hpp"
#include "settings/settings.hpp"
#if defined(MHP2G_HAS_RENDERER)
#include "ui/ui.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace mhp2g::fast_forward {

namespace {

using Clock = std::chrono::steady_clock;

struct State {
    Switch control;
    // The stretch running now: where it started, in both clocks.
    Clock::time_point real_start{};
    std::uint64_t emulated_start{};
    std::uint64_t vblank_start{};
    std::uint32_t speed{};
};

State &state() {
    static State instance;
    return instance;
}

bool windowed() {
    static const bool value = std::getenv("MHP2G_NO_RENDER") == nullptr;
    return value;
}

Guards sample_guards() {
    Guards guards;
    // Unlimited already runs the game unpaced, and without a window nothing
    // holds it to real time either.
    guards.available = windowed() && !settings::current().unthrottled;
    guards.online = adhoc_networking_on() || adhoc_session_active();
#if defined(MHP2G_HAS_RENDERER)
    guards.menu = ui::menu_over_game();
#endif
    return guards;
}

std::uint32_t setting_speed() {
    return std::clamp(settings::current().fast_forward_speed, kMinSpeed, kMaxSpeed);
}

void begin_stretch() {
    State &s = state();
    s.real_start = Clock::now();
    s.emulated_start = kernel().now_us();
    s.vblank_start = kernel().vblank_count();
    s.speed = setting_speed();
    std::printf("[fast-forward] on, %ux\n", s.speed);
    std::fflush(stdout);
}

// One line per stretch, with the vblanks the game saw in it: they are the
// emulated time divided by the vblank period, however fast it ran.
void end_stretch(Reason reason) {
    State &s = state();
    const double real_ms = std::chrono::duration<double, std::milli>(Clock::now() - s.real_start).count();
    const double emulated_ms = static_cast<double>(kernel().now_us() - s.emulated_start) / 1000.0;
    const std::uint64_t vblanks = kernel().vblank_count() - s.vblank_start;
    std::printf("[fast-forward] off (%s): %.0f ms of game time, %llu vblanks, in %.0f ms real (%.2fx)\n",
        reason_name(reason), emulated_ms, static_cast<unsigned long long>(vblanks), real_ms,
        real_ms > 0.0 ? emulated_ms / real_ms : 0.0);
    std::fflush(stdout);
}

} // namespace

void note_bind(bool held) {
    State &s = state();
    const bool was_active = s.control.active();
    const bool now_active = s.control.update(held, settings::current().fast_forward, sample_guards());
    if (s.control.take_refused_press())
        log_once("fast-forward-adhoc", "[fast-forward] single player only: the bind does nothing during ad hoc play");
    if (now_active && !was_active)
        begin_stretch();
    else if (!now_active && was_active)
        end_stretch(s.control.reason());
    else if (now_active && setting_speed() != s.speed) {
        // The speed changed in the menu while it ran.
        end_stretch(Reason::None);
        begin_stretch();
    }
}

bool active() {
    return state().control.active();
}

double speed() {
    return active() ? static_cast<double>(state().speed) : 1.0;
}

} // namespace mhp2g::fast_forward
