// Fast-forward's switch: holding and toggling the bind, and everything that
// keeps real time whatever the bind says. No game data.
#include "kernel/fast_forward.hpp"

#include <iostream>

namespace {
using namespace mhp3rd::fast_forward;

int failures{};

void check(bool condition, const char *message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

Guards open_guards() {
    Guards guards;
    guards.available = true;
    return guards;
}

void test_hold() {
    Switch s;
    check(!s.update(false, Mode::Hold, open_guards()), "not held: real time");
    check(s.reason() == Reason::Released, "because the bind is up");
    check(s.update(true, Mode::Hold, open_guards()), "held: fast");
    check(s.update(true, Mode::Hold, open_guards()), "still held: still fast");
    check(!s.update(false, Mode::Hold, open_guards()), "released: real time at once");
}

void test_toggle() {
    Switch s;
    check(s.update(true, Mode::Toggle, open_guards()), "the first press turns it on");
    check(s.update(false, Mode::Toggle, open_guards()), "and it stays on after the release");
    check(s.update(true, Mode::Toggle, open_guards()) == false, "the next press turns it off");
    check(!s.update(true, Mode::Toggle, open_guards()), "holding that press does not turn it on again");
    check(!s.update(false, Mode::Toggle, open_guards()), "off after the release");
}

void test_off_and_unavailable() {
    Switch s;
    check(!s.update(true, Mode::Off, open_guards()), "the setting off: the bind does nothing");
    check(s.reason() == Reason::Off, "because it is off");
    Guards guards = open_guards();
    guards.available = false;
    check(!s.update(true, Mode::Hold, guards), "no window or Unlimited: nothing to do");
    check(s.reason() == Reason::Unavailable, "because it is unavailable");
}

void test_ad_hoc() {
    Switch s;
    Guards online = open_guards();
    online.online = true;
    check(!s.update(true, Mode::Hold, online), "never during ad hoc play");
    check(s.reason() == Reason::Online, "because of ad hoc play");
    check(s.take_refused_press(), "the press is reported");
    check(!s.take_refused_press(), "once");
    check(!s.update(true, Mode::Hold, online), "holding on is not a new press");
    check(!s.take_refused_press(), "so nothing more is reported");

    Switch t;
    check(t.update(true, Mode::Toggle, open_guards()), "toggled on in single player");
    check(!t.update(false, Mode::Toggle, online), "ad hoc play ends it at once");
    check(!t.update(false, Mode::Toggle, open_guards()), "and it does not come back afterwards on its own");
}

void test_menu() {
    Switch s;
    check(s.update(true, Mode::Toggle, open_guards()), "toggled on");
    Guards menu = open_guards();
    menu.menu = true;
    check(!s.update(false, Mode::Toggle, menu), "the menu over the game keeps real time");
    check(s.reason() == Reason::Menu, "because of the menu");
    check(s.update(false, Mode::Toggle, open_guards()), "the toggle carries on once the menu closes");
}

} // namespace

int main() {
    test_hold();
    test_toggle();
    test_off_and_unavailable();
    test_ad_hoc();
    test_menu();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "fast forward tests passed\n";
    return 0;
}
