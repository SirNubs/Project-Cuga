// The part of fast-forward that decides, kept apart from the host so the
// tests can drive it (tests/fast_forward_tests.cpp).
#include "kernel/fast_forward.hpp"

namespace mhp2g::fast_forward {

const char *reason_name(Reason reason) {
    switch (reason) {
    case Reason::None:
        return "none";
    case Reason::Released:
        return "released";
    case Reason::Off:
        return "off";
    case Reason::Unavailable:
        return "unavailable";
    case Reason::Online:
        return "ad hoc";
    case Reason::Menu:
        return "menu";
    }
    return "?";
}

bool Switch::update(bool held, Mode mode, const Guards &guards) {
    const bool pressed = held && !was_held_;
    was_held_ = held;

    if (mode == Mode::Off || !guards.available) {
        toggled_ = false;
        active_ = false;
        reason_ = mode == Mode::Off ? Reason::Off : Reason::Unavailable;
        return false;
    }
    if (guards.online) {
        // Never during ad hoc play, and a toggle made before it does not come
        // back afterwards on its own.
        if (pressed) refused_press_ = true;
        toggled_ = false;
        active_ = false;
        reason_ = Reason::Online;
        return false;
    }
    if (mode == Mode::Toggle) {
        if (pressed) toggled_ = !toggled_;
    } else {
        toggled_ = false;
    }
    const bool wanted = mode == Mode::Toggle ? toggled_ : held;
    if (!wanted) {
        active_ = false;
        reason_ = Reason::Released;
        return false;
    }
    // The menu over the running game takes the input; a toggle waits for it
    // to close.
    if (guards.menu) {
        active_ = false;
        reason_ = Reason::Menu;
        return false;
    }
    active_ = true;
    reason_ = Reason::None;
    return true;
}

} // namespace mhp2g::fast_forward
