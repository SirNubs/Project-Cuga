// The on-screen touch controls' layout, fingers and stick, without a screen.
#include "input/touch_action.hpp"
#include "input/touch_controls.hpp"

#include <cmath>
#include <iostream>

namespace {
using namespace mhp3rd::input::touch;

int failures{};

void check(bool condition, const char *message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

const Circle &at(const Layout &layout, Control control) {
    return layout.controls[static_cast<std::size_t>(control)];
}

void test_layout_fits(float width, float height, Insets insets, float size, const char *what) {
    const Layout layout = make_layout(width, height, insets, size);
    bool inside = true;
    for (const Circle &c : layout.controls)
        inside = inside && c.centre.x - c.radius >= insets.left - 0.5f &&
            c.centre.x + c.radius <= width - insets.right + 0.5f && c.centre.y - c.radius >= insets.top - 0.5f &&
            c.centre.y + c.radius <= height - insets.bottom + 0.5f;
    check(inside, what);
    bool apart = true;
    for (std::size_t i = 0; i < kControls; ++i)
        for (std::size_t j = i + 1; j < kControls; ++j) {
            const Circle &a = layout.controls[i];
            const Circle &b = layout.controls[j];
            apart = apart && std::hypot(a.centre.x - b.centre.x, a.centre.y - b.centre.y) >= a.radius + b.radius;
        }
    check(apart, "no two controls overlap");
    const Circle &d = layout.dpad;
    check(d.centre.x - d.radius >= insets.left - 0.5f && d.centre.y - d.radius >= insets.top - 0.5f &&
            d.centre.y + d.radius <= height - insets.bottom + 0.5f && d.centre.x + d.radius < layout.stick_split,
        "the D-pad is on the left, on screen");
    bool dpad_apart = true;
    for (const Circle &c : layout.controls)
        dpad_apart = dpad_apart && std::hypot(c.centre.x - d.centre.x, c.centre.y - d.centre.y) >= c.radius + d.radius;
    check(dpad_apart, "the D-pad overlaps no button");
    check(at(layout, Control::Cross).centre.x > layout.stick_split, "the face buttons are on the right");
    check(at(layout, Control::L).centre.x < layout.stick_split, "L is on the left");
    check(at(layout, Control::L).centre.y < height * 0.3f && at(layout, Control::R).centre.y < height * 0.3f,
        "the shoulders are at the top");
    check(at(layout, Control::Triangle).centre.y < at(layout, Control::Cross).centre.y &&
            at(layout, Control::Square).centre.x < at(layout, Control::Circle).centre.x,
        "the face buttons make the PSP's diamond");
}

void test_layouts() {
    test_layout_fits(1280.0f, 576.0f, {}, 1.0f, "20:9 fits");
    test_layout_fits(1920.0f, 1080.0f, {}, 1.0f, "16:9 fits");
    test_layout_fits(2400.0f, 1080.0f, {128.0f, 0.0f, 0.0f, 0.0f}, 1.0f, "a cutout on the left is kept clear");
    test_layout_fits(2400.0f, 1080.0f, {0.0f, 0.0f, 128.0f, 0.0f}, 1.5f, "larger controls still fit");
    test_layout_fits(1280.0f, 800.0f, {}, 0.6f, "16:10 at a small size fits");
}

void test_stick_maths() {
    check(stick_deflection({0.0f, 0.0f}, 100.0f).x == 0.0f, "no push at the centre");
    check(stick_deflection({10.0f, 0.0f}, 100.0f).x == 0.0f, "a twitch inside the dead zone is nothing");
    const Point full = stick_deflection({200.0f, 0.0f}, 100.0f);
    check(std::fabs(full.x - 1.0f) < 1e-5f && full.y == 0.0f, "beyond the reach is full deflection");
    const Point half = stick_deflection({0.0f, -56.0f}, 100.0f);
    check(half.y < -0.45f && half.y > -0.55f, "half way is about half");
    const Point diagonal = stick_deflection({100.0f, 100.0f}, 100.0f);
    check(std::fabs(std::hypot(diagonal.x, diagonal.y) - 1.0f) < 1e-4f, "a diagonal is not more than full");
}

void test_dpad() {
    const float r = 100.0f;
    check(dpad_buttons({0.0f, -80.0f}, r) == 0x10u, "up");
    check(dpad_buttons({80.0f, 0.0f}, r) == 0x20u, "right");
    check(dpad_buttons({0.0f, 80.0f}, r) == 0x40u, "down");
    check(dpad_buttons({-80.0f, 0.0f}, r) == 0x80u, "left");
    check(dpad_buttons({60.0f, -60.0f}, r) == 0x30u, "up and right together");
    check(dpad_buttons({-60.0f, 60.0f}, r) == 0xC0u, "down and left together");
    check(dpad_buttons({80.0f, -20.0f}, r) == 0x20u, "a little off right is still right alone");
    check(dpad_buttons({5.0f, 5.0f}, r) == 0u, "the middle presses nothing");

    Controls controls;
    const Layout layout = make_layout(1280.0f, 576.0f, {}, 1.0f);
    controls.set_layout(layout);
    const Point centre = layout.dpad.centre;
    controls.finger_down(1, {centre.x, centre.y - layout.dpad.radius * 0.7f});
    check(controls.buttons() == 0x10u, "a finger on the D-pad's top arm presses up");
    controls.finger_move(1, {centre.x + layout.dpad.radius * 0.7f, centre.y});
    check(controls.buttons() == 0x20u, "sliding to the right arm presses right without lifting");
    // Owned until lifted: a D-pad finger dragged far away stays the D-pad's,
    // never the stick's or the camera's.
    controls.finger_move(1, {centre.x + 600.0f, centre.y});
    check(controls.buttons() == 0x20u && !controls.stick_state().active, "a D-pad finger stays the D-pad's");
    check(controls.take_camera_drag().x == 0.0f, "and never turns the camera");
    controls.finger_up(1);
    check(controls.buttons() == 0u, "lifting it lets go");

    const Layout hidden = make_layout(1280.0f, 576.0f, {}, 1.0f, false);
    controls.set_layout(hidden);
    controls.finger_down(2, centre);
    check(controls.buttons() == 0u && controls.stick_state().active, "without the D-pad its place is the stick's");
}

void test_fingers() {
    Controls controls;
    const Layout layout = make_layout(1280.0f, 576.0f, {}, 1.0f);
    controls.set_layout(layout);

    // A thumb on the left moves; one on Cross presses; one on the free right
    // half turns the camera; all at once.
    controls.finger_down(1, {200.0f, 400.0f});
    controls.finger_move(1, {200.0f + layout.stick_radius, 400.0f});
    controls.finger_down(2, at(layout, Control::Cross).centre);
    controls.finger_down(3, {800.0f, 250.0f});
    controls.finger_move(3, {830.0f, 240.0f});
    check(std::fabs(controls.stick().x - 1.0f) < 1e-4f, "the stick pushes right");
    check(controls.buttons() == 0x4000u, "Cross is held");
    const Point drag = controls.take_camera_drag();
    check(drag.x == 30.0f && drag.y == -10.0f, "the camera drag is what the finger moved");
    check(controls.take_camera_drag().x == 0.0f, "a drag is taken once");

    // Sliding from Cross to Circle moves the press; off the buttons, nothing.
    controls.finger_move(2, at(layout, Control::Circle).centre);
    check(controls.buttons() == 0x2000u, "sliding onto Circle presses it instead");
    controls.finger_move(2, {640.0f, 300.0f});
    check(controls.buttons() == 0u, "sliding off the buttons lets go");

    // The stick follows a thumb dragged past its reach.
    controls.finger_move(1, {200.0f + layout.stick_radius * 3.0f, 400.0f});
    controls.finger_move(1, {200.0f + layout.stick_radius * 1.5f, 400.0f});
    check(controls.stick().x < 0.0f, "coming back from a long drag pushes the other way at once");

    controls.finger_up(1);
    check(controls.stick().x == 0.0f && !controls.stick_state().active, "lifting the thumb centres the stick");
    controls.finger_down(4, at(layout, Control::Menu).centre);
    check(controls.take_menu() && !controls.take_menu(), "the menu button is one tap");
    check(controls.buttons() == 0u, "the menu button presses no PSP button");
    controls.release_all();
    check(!controls.any_finger() && controls.buttons() == 0u, "release_all lets go of everything");
}

// The action layout (#174).
void test_action_layout_fits(float width, float height, Insets insets, float scale, const char *what) {
    ActionControls controls;
    const Area area = safe_area(width, height, insets);
    controls.set_layout(default_action_layout(), area, scale);
    bool inside = true;
    bool apart = true;
    for (std::size_t i = 0; i < kElements; ++i) {
        const Placed &p = controls.placed(static_cast<Element>(i));
        inside = inside && p.centre.x - p.half_width >= area.left - 0.5f &&
            p.centre.x + p.half_width <= area.left + area.width + 0.5f && p.centre.y - p.radius >= area.top - 0.5f &&
            p.centre.y + p.radius <= area.top + area.height + 0.5f;
        if (static_cast<Element>(i) == Element::Swipe) continue;
        for (std::size_t j = i + 1; j < kElements; ++j) {
            if (static_cast<Element>(j) == Element::Swipe) continue;
            const Placed &q = controls.placed(static_cast<Element>(j));
            apart = apart && std::hypot(p.centre.x - q.centre.x, p.centre.y - q.centre.y) >= p.radius + q.radius;
        }
    }
    check(inside, what);
    check(apart, "no two elements of the action layout overlap");
}

void test_action_layout() {
    test_action_layout_fits(2400.0f, 1080.0f, {}, 1.0f, "the action layout fits 20:9");
    test_action_layout_fits(1920.0f, 1080.0f, {}, 1.0f, "the action layout fits 16:9");
    test_action_layout_fits(2400.0f, 1080.0f, {120.0f, 0.0f, 0.0f, 0.0f}, 1.0f, "and keeps clear of a cutout");
    test_action_layout_fits(1600.0f, 1080.0f, {}, 1.0f, "and fits a wide tablet");

    for (std::size_t i = 0; i < kElements; ++i) {
        const Placement &p = default_action_layout().elements[i];
        Placement back;
        check(parse(format(p), back) && back.anchor == p.anchor && std::fabs(back.x - p.x) < 1e-3f &&
                std::fabs(back.y - p.y) < 1e-3f && std::fabs(back.size - p.size) < 1e-3f && back.buttons == p.buttons &&
                back.shown == p.shown,
            "every placement round-trips through settings.ini");
    }
    Placement placement;
    check(!parse("middle 0 0 0.1 0x1000 1", placement) && !parse("left 0 0", placement), "bad placements are refused");
    check(default_action_layout().at(Element::Combo).buttons == 0x3000u, "the combined attack is △ and ○ together");

    const Area area = safe_area(2000.0f, 1000.0f, {});
    const Placement moved =
        move_to(default_action_layout().at(Element::Attack), Element::Attack, {300.0f, 500.0f}, area);
    const Placed there = place(moved, Element::Attack, area, 1.0f);
    check(moved.anchor == Anchor::Left && std::fabs(there.centre.x - 300.0f) < 0.01f &&
            std::fabs(there.centre.y - 500.0f) < 0.01f,
        "moving an element puts it where it is dropped, anchored to the nearer edge");
}

void test_action_fingers() {
    ActionControls controls;
    const Area area = safe_area(2000.0f, 1000.0f, {});
    controls.set_layout(default_action_layout(), area, 1.0f);
    const auto centre = [&](Element e) { return controls.placed(e).centre; };
    std::uint64_t ms = 1000u;

    controls.finger_down(1, centre(Element::Attack), ms);
    check(controls.buttons(ms) == 0x1000u && controls.held(Element::Attack), "the attack presses △");
    check(controls.take_haptics() == 1 && controls.take_haptics() == 0, "a press gives one haptic tick");
    controls.finger_move(1, centre(Element::Evade), ms + 10u);
    check(controls.buttons(ms) == 0x4000u, "a thumb slides from the attack to evade");
    controls.finger_up(1, ms + 20u);
    controls.finger_down(2, centre(Element::Combo), ms);
    check(controls.buttons(ms) == 0x3000u, "the combined attack presses △ and ○ in the same frame");

    // The stick, a button and the camera at once.
    const Point stick = centre(Element::Stick);
    controls.finger_down(3, {stick.x + 20.0f, stick.y}, ms);
    controls.finger_move(3, {stick.x + controls.placed(Element::Stick).radius, stick.y}, ms);
    controls.finger_down(4, {1000.0f, 900.0f}, ms);
    controls.finger_move(4, {1050.0f, 880.0f}, ms + 5u);
    check(controls.stick().x > 0.99f && std::fabs(controls.stick().y) < 1e-3f, "the stick is fixed where it is placed");
    check(controls.buttons(ms) == 0x3000u, "while a button is held");
    const Point drag = controls.take_camera_drag();
    check(drag.x == 50.0f && drag.y == -20.0f, "and a free finger turns the camera");
    controls.finger_up(3, ms);
    check(controls.stick().x == 0.0f && !controls.stick_held(), "lifting the thumb centres the stick");
    controls.release_all();

    // A quick swipe presses D-pad Right for a moment; a slow drag there turns the camera.
    const Point swipe = centre(Element::Swipe);
    const float reach = ActionControls::kSwipeDistance * area.height;
    controls.finger_down(5, swipe, ms);
    controls.finger_move(5, {swipe.x + reach * 1.2f, swipe.y + 5.0f}, ms + 80u);
    check(controls.buttons(ms + 80u) == 0x0020u, "a swipe right presses D-pad Right");
    check(controls.buttons(ms + 80u + ActionControls::kSwipePressMs) == 0u, "for a moment");
    controls.finger_move(5, {swipe.x + reach * 2.5f, swipe.y}, ms + 150u);
    check(controls.buttons(ms + 150u) == 0x0020u, "a longer swipe presses again");
    controls.finger_move(5, {swipe.x + reach * 1.2f, swipe.y}, ms + 200u);
    check(controls.buttons(ms + 200u) == 0x0080u, "and swiping back presses Left, not both");
    check(controls.take_camera_drag().x == 0.0f, "a swipe does not turn the camera");
    controls.finger_up(5, ms + 250u);
    controls.finger_down(6, swipe, ms + 1000u);
    controls.finger_move(6, {swipe.x + reach * 0.3f, swipe.y}, ms + 1100u);
    controls.finger_move(6, {swipe.x + reach * 1.5f, swipe.y}, ms + 1400u);
    check(controls.buttons(ms + 1400u) == 0u, "a slow drag in the swipe area presses nothing");
    check(std::fabs(controls.take_camera_drag().x - reach * 1.5f) < 0.01f, "it turns the camera, all of it");
    controls.release_all();

    controls.finger_down(7, centre(Element::Pause), ms);
    check(controls.take_menu() && !controls.take_menu() && controls.buttons(ms) == 0u,
        "Pause opens the menu and presses nothing");
    controls.release_all();

    // A hidden element is not there, and a rebound one presses what it is bound to.
    ActionLayout changed = default_action_layout();
    changed.at(Element::Secondary).shown = false;
    changed.at(Element::Guard).buttons = 0x0200u | 0x1000u;
    controls.set_layout(changed, area, 1.0f);
    controls.finger_down(8, centre(Element::Secondary), ms);
    check(controls.buttons(ms) == 0u, "a hidden button presses nothing");
    controls.finger_down(9, centre(Element::Guard), ms);
    check(controls.buttons(ms) == 0x1200u, "a rebound button presses its combination");
}

} // namespace

int main() {
    test_layouts();
    test_stick_maths();
    test_dpad();
    test_fingers();
    test_action_layout();
    test_action_fingers();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "touch controls tests passed\n";
    return 0;
}
