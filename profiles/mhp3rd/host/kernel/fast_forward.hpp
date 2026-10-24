#pragma once

#include <cstdint>

// Fast-forward (#166): while the player holds its bind, or after toggling it,
// emulated time runs a set number of times faster than real time.
//
// There is no second clock. The kernel's hold of emulated time to real time
// (Kernel::pace_to_real_time) lets emulated time run `speed()` times as fast
// as real time, the way fast loading lets it run ahead during a load
// (kernel/fast_loading.hpp). Every vblank still comes kVBlankPeriodUs of
// emulated time after the one before, so the game runs each of its frames,
// its timers and its sound thread exactly as a PSP would; they only arrive
// sooner in real time.
//
// While it runs, the game's sound is not played (the buffers are dropped, as
// a fast load drops its silence), the window shows a flip at most about 30
// times a second, frame interpolation waits, and a small indicator is drawn
// over the game by the interface, never into the game's own frames.
//
// Single player only: during ad hoc play the other players' time is real, so
// the bind does nothing there, and a press says so once on the console.
namespace mhp3rd::fast_forward {

// What the bind does, from the settings.
enum class Mode { Hold, Toggle, Off };

inline constexpr std::uint32_t kMinSpeed = 2u;
inline constexpr std::uint32_t kMaxSpeed = 8u;
inline constexpr std::uint32_t kDefaultSpeed = 3u;

// What keeps real time whatever the bind says, sampled at each update.
struct Guards {
    bool available{}; // there is a window, and Game speed is Normal
    bool online{};    // ad hoc networking is on, or a session is going
    bool menu{};      // the menu is open over the running game
};

enum class Reason { None, Released, Off, Unavailable, Online, Menu };
[[nodiscard]] const char *reason_name(Reason reason);

// The decision itself, without the host around it, so tests can drive it.
class Switch {
public:
    // The bind is `held` now. Returns whether fast-forward runs.
    bool update(bool held, Mode mode, const Guards &guards);

    [[nodiscard]] bool active() const noexcept { return active_; }
    // Why the last update left it off; None while it runs.
    [[nodiscard]] Reason reason() const noexcept { return reason_; }
    // Once per press made while ad hoc play keeps it off.
    [[nodiscard]] bool take_refused_press() noexcept {
        const bool value = refused_press_;
        refused_press_ = false;
        return value;
    }

private:
    bool was_held_{};
    bool toggled_{};
    bool active_{};
    bool refused_press_{};
    Reason reason_{Reason::Released};
};

// The running game's switch, called on the emulation thread at each flip with
// the bind's state from the pad sampled for it.
void note_bind(bool held);
// Whether emulated time runs fast now.
[[nodiscard]] bool active();
// How many times faster than real time emulated time may run now: the
// setting's speed while active, 1 otherwise.
[[nodiscard]] double speed();

} // namespace mhp3rd::fast_forward
