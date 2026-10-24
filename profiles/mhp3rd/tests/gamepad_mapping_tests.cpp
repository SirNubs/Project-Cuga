// Gamepad mappings for unknown controllers (#147), without SDL.
#include "input/gamepad_mapping.hpp"

#include <iostream>
#include <string>

namespace {
using namespace mhp3rd::input::mapping;
int failures{};

void check(bool condition, const char *message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

Element button(int index) {
    return {Element::Kind::Button, index};
}
Element hat(int index, int mask) {
    Element e{Element::Kind::Hat, index};
    e.hat_mask = mask;
    return e;
}
Element axis(int index, Element::Range range = Element::Range::Full, bool inverted = false) {
    Element e{Element::Kind::Axis, index};
    e.range = range;
    e.inverted = inverted;
    return e;
}
Answers &set(Answers &answers, Target target, Element element) {
    answers[static_cast<std::size_t>(target)] = element;
    return answers;
}

void test_elements() {
    check(text(button(3)) == "b3", "a button is b and its number");
    check(text(hat(0, 4)) == "h0.4", "a hat is h, its number and the direction's bit");
    check(text(axis(2)) == "a2", "a whole axis is a and its number");
    check(text(axis(1, Element::Range::Positive)) == "+a1" && text(axis(1, Element::Range::Negative)) == "-a1",
        "half an axis has its sign in front");
    check(text(axis(0, Element::Range::Full, true)) == "a0~", "an inverted axis ends in a tilde");
    for (const Element &e :
        {button(11), hat(1, 8), axis(5), axis(3, Element::Range::Negative), axis(4, Element::Range::Full, true)})
        check(parse_element(text(e)) == e, "every element reads back as written");
    check(parse_element("q7").empty() && parse_element("").empty() && parse_element("b").empty(),
        "anything else is no element");
    check(describe(hat(0, 2)) == "Hat 0 right" && describe(button(0)) == "Button 0" &&
            describe(axis(1, Element::Range::Positive)) == "Axis 1 +",
        "the screen names elements in words");
    check(button(2).same_input(button(2)) && !button(2).same_input(button(3)) &&
            axis(1, Element::Range::Positive).same_input(axis(1, Element::Range::Negative)) &&
            !hat(0, 1).same_input(hat(0, 4)),
        "the same input is the same button, hat direction or axis");
}

void test_build() {
    Answers a{};
    set(a, Target::A, button(2));
    set(a, Target::B, button(1));
    set(a, Target::X, button(3));
    set(a, Target::Y, button(0));
    set(a, Target::DpadUp, hat(0, 1));
    set(a, Target::DpadDown, hat(0, 4));
    set(a, Target::LeftShoulder, button(6));
    set(a, Target::Start, button(9));
    set(a, Target::LeftX, axis(0));
    set(a, Target::LeftY, axis(1));
    set(a, Target::RightX, axis(3));
    set(a, Target::RightY, axis(2, Element::Range::Full, true));
    const std::string line = build("03000000100800000100000000000000", "PS to USB, twin", a, "Windows");
    check(line ==
            "03000000100800000100000000000000,PS to USB  twin,a:b2,b:b1,x:b3,y:b0,dpup:h0.1,dpdown:h0.4,"
            "leftshoulder:b6,start:b9,leftx:a0,lefty:a1,rightx:a3,righty:a2~,platform:Windows,",
        "the mapping line lists the answers in order and ends with the platform");
    check(guid_of(line) == "03000000100800000100000000000000" && platform_of(line) == "Windows",
        "the GUID and the platform read back");
    check(answers_of(line) == a, "and so do the answers");
    check(count(a) == 12u, "the answers are counted");
    Answers none{};
    check(build("00", "Pad", none, "") == "00,Pad,", "no answers, no platform: only the GUID and the name");
    check(clean_name(" A,B\n ") == "A B" && clean_name(",") == "Controller",
        "names lose commas and line breaks, and are never empty");
    check(field(Target::DpadLeft) == std::string("dpleft") && field(Target::RightY) == std::string("righty") &&
            is_axis(Target::LeftX) && !is_axis(Target::LeftTrigger),
        "targets have SDL's field names");
    // SDL's own line for the adapter of #147 reads back.
    const Answers sdl = answers_of("03000000100800000100000000000000,PS1 Controller,a:b2,b:b1,back:b8,dpdown:h0.4,"
                                   "lefttrigger:b4,leftx:a0,righty:a2,start:b9,platform:Windows,");
    check(sdl[static_cast<std::size_t>(Target::A)] == button(2) &&
            sdl[static_cast<std::size_t>(Target::DpadDown)] == hat(0, 4) &&
            sdl[static_cast<std::size_t>(Target::RightY)] == axis(2) && count(sdl) == 8u,
        "SDL's own lines read back");
}

void test_file() {
    const std::string guid = "0300000010080000010000000000abcd";
    const std::string first = guid + ",Pad,a:b0,platform:Linux,";
    std::string text = with_line("", first);
    check(
        text.rfind("# ", 0) == 0 && find_line(text, guid, "Linux") == first, "a new file gets a comment and the line");
    check(lines(text).size() == 1u, "comments are no mappings");
    const std::string second = guid + ",Pad,a:b1,platform:Linux,";
    text = with_line(text, second);
    check(find_line(text, guid, "Linux") == second && lines(text).size() == 1u, "a new line replaces the old one");
    const std::string windows = guid + ",Pad,a:b2,platform:Windows,";
    text = with_line(text, windows);
    check(lines(text).size() == 2u && find_line(text, guid, "Windows") == windows &&
            find_line(text, guid, "Linux") == second,
        "another platform's line for the same GUID is kept");
    const std::string other = "03000000aaaa0000bbbb000000000000,Other,a:b0,platform:Linux,";
    text += "\r\n" + other + "\r\n";
    check(find_line(text, "03000000AAAA0000BBBB000000000000", "Linux") == other,
        "GUIDs match ignoring case, and Windows line breaks are read");
    bool removed = false;
    text = without(text, guid, "Linux", &removed);
    check(removed && !find_line(text, guid, "Linux") && find_line(text, guid, "Windows") &&
            find_line(text, other.substr(0, 32), "Linux"),
        "removing takes only that GUID's line for that platform");
    (void)without(text, guid, "Linux", &removed);
    check(!removed, "and says when there was none");
    check(find_line("# 0300,comment\n", "0300", "") == std::nullopt, "comment lines are never mappings");
    const std::vector<std::string> env = lines("a,Pad,a:b0\n\nb,Pad2,a:b1\n");
    check(env.size() == 2u && env[1] == "b,Pad2,a:b1", "an environment variable may hold several lines");
}

Snapshot snap(std::vector<bool> buttons, std::vector<std::uint8_t> hats, std::vector<std::int16_t> axes) {
    return {std::move(buttons), std::move(hats), std::move(axes)};
}

void test_detect() {
    // 12 buttons, a hat and four axes, like a PS2 pad on a USB adapter.
    const Snapshot rest = snap(std::vector<bool>(12, false), {0}, {0, 0, 0, 0});
    check(!detect(rest, rest, false) && at_rest(rest, rest), "nothing moved, nothing found");
    Snapshot now = rest;
    now.buttons[5] = true;
    check(detect(rest, now, false) == button(5) && !at_rest(rest, now), "a pressed button is found");
    check(!detect(rest, now, true), "but not for a stick");
    now = rest;
    now.hats[0] = 2;
    check(detect(rest, now, false) == hat(0, 2), "a hat pushed one way is found");
    now.hats[0] = 3;
    check(!detect(rest, now, false), "a diagonal waits");
    now = rest;
    now.axes[1] = 20000;
    check(detect(rest, now, false) == axis(1, Element::Range::Positive), "a centred axis gives the half it moved to");
    check(detect(rest, now, true) == axis(1), "and a stick the whole axis");
    now.axes[1] = -20000;
    check(detect(rest, now, true) == axis(1, Element::Range::Full, true), "a stick moved the other way is inverted");
    now.axes[1] = 9000;
    check(!detect(rest, now, false) && !at_rest(rest, now), "half-way is neither pressed nor back at rest");
    now.axes[1] = 4000;
    check(at_rest(rest, now), "near its rest the axis is back");
    // A trigger resting at the bottom of its axis.
    Snapshot trigger_rest = rest;
    trigger_rest.axes[3] = -32768;
    now = trigger_rest;
    now.axes[3] = 32767;
    check(detect(trigger_rest, now, false) == axis(3), "a trigger is its whole axis");
    check(!detect(trigger_rest, now, true), "and no stick");
    trigger_rest.axes[3] = 32767;
    now = trigger_rest;
    now.axes[3] = -32768;
    check(detect(trigger_rest, now, false) == axis(3, Element::Range::Full, true),
        "a trigger resting at the top is inverted");
    // Buttons first, then hats, then axes.
    now = rest;
    now.buttons[0] = true;
    now.hats[0] = 1;
    now.axes[0] = 32767;
    check(detect(rest, now, false) == button(0), "a button wins over a hat and an axis");
    now.buttons[0] = false;
    check(detect(rest, now, false) == hat(0, 1), "a hat wins over an axis");
    // The axis moved furthest.
    now = rest;
    now.axes[0] = 17000;
    now.axes[2] = -30000;
    check(detect(rest, now, true) == axis(2, Element::Range::Full, true), "the axis moved furthest is taken");
    // A button held since before the setup started does not count.
    Snapshot held = rest;
    held.buttons[7] = true;
    check(!detect(held, held, false) && at_rest(held, held), "a button held at rest is ignored");
}

} // namespace

int main() {
    test_elements();
    test_build();
    test_file();
    test_detect();
    if (failures != 0) {
        std::cerr << failures << " gamepad mapping check(s) failed\n";
        return 1;
    }
    std::cout << "gamepad mapping tests passed\n";
    return 0;
}
