#pragma once

#include "input/touch_controls.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The action-style touch layout (#174), an alternative to the PSP-button
// overlay in touch_controls.hpp: large buttons named for what they do, each
// pressing a PSP button or a combination of them in the same frame, a fixed
// stick, an item button (L), D-pad Up and Down, a swipe area whose left and
// right swipes press the D-pad's Left and Right, and Pause, Start and Select.
// Every element can be moved, resized, rebound and hidden, and the layout is
// kept in settings.ini. This part is pure logic, like touch_controls: where
// the elements are on a screen, which finger holds what, and what comes out.
namespace mhp3rd::input::touch {

enum class Element : std::uint8_t {
    Stick, // the analog stick, fixed where it is placed
    Item,  // L: the item pouch
    DpadUp,
    DpadDown,
    Attack,    // the large main attack: △
    Secondary, // ○
    Combo,     // △ + ○ in one frame: the combined attack
    Evade,     // ✕
    Use,       // □: sheathe, use an item, gather
    Guard,     // R
    Special,   // R as well, where the other thumb reaches it
    Pause,     // Yakumo's menu
    Start,
    Select,
    Swipe, // the swipe area: left and right swipes press D-pad Left and Right
    Count
};
inline constexpr std::size_t kElements = static_cast<std::size_t>(Element::Count);

// Which edge of the safe area an element keeps its distance from, so the
// layout fits screens of any width: the left thumb's elements to the left
// edge, the right thumb's to the right one, the swipe area to the middle.
enum class Anchor : std::uint8_t { Left, Centre, Right };

struct Placement {
    Anchor anchor{Anchor::Left};
    float x{};               // the centre from the anchor (right: leftwards), in heights of the safe area
    float y{};               // the centre from the safe area's top, in heights of the safe area
    float size{};            // the radius in heights of the safe area; the swipe area's half height
    std::uint16_t buttons{}; // SceCtrlButtons pressed; ignored for the stick, Pause and the swipe area
    bool shown{true};
    friend bool operator==(const Placement &, const Placement &) = default;
};

struct ActionLayout {
    std::array<Placement, kElements> elements{};
    friend bool operator==(const ActionLayout &, const ActionLayout &) = default;
    [[nodiscard]] Placement &at(Element e) { return elements[static_cast<std::size_t>(e)]; }
    [[nodiscard]] const Placement &at(Element e) const { return elements[static_cast<std::size_t>(e)]; }
};

// Where the layout starts, and what "Reset" goes back to.
[[nodiscard]] const ActionLayout &default_action_layout();

struct ElementInfo {
    const char *key;  // in settings.ini, after "input.touch_action."
    const char *name; // in the editor
};
[[nodiscard]] const ElementInfo &info(Element element);
// Whether the editor may change what an element presses.
[[nodiscard]] bool rebindable(Element element);
// The swipe area is a rounded rectangle this many times as wide as high.
inline constexpr float kSwipeAspect = 3.2f;
inline constexpr float kMinElementSize = 0.02f;
inline constexpr float kMaxElementSize = 0.25f;

// What a touch button can press: one PSP button or two in the same frame.
struct ButtonChoice {
    std::uint16_t buttons;
    const char *label;
};
[[nodiscard]] const std::vector<ButtonChoice> &button_choices();
// "△", "△ + ○", "D-pad Up"; the bits in hexadecimal for anything else.
[[nodiscard]] std::string buttons_label(std::uint16_t buttons);

// settings.ini's spelling of a placement: "right 0.355 0.780 0.085 0x1000 1".
[[nodiscard]] std::string format(const Placement &placement);
bool parse(std::string_view text, Placement &placement);

// An element in window pixels, for a layout on a screen.
struct Placed {
    Point centre;
    float radius{};     // for the swipe area, its half height
    float half_width{}; // for the swipe area; the radius for the others
};
struct Area {
    float left{};
    float top{};
    float width{};
    float height{};
};
// The safe area of a width x height screen, clear of `insets`.
[[nodiscard]] Area safe_area(float width, float height, Insets insets);
// `scale` multiplies every element's size (the on-screen controls' size).
[[nodiscard]] Placed place(const Placement &placement, Element element, const Area &area, float scale);
// The reverse, for the editor: the placement that puts the element's centre
// at `centre`, anchored to the nearer edge (or the middle for the swipe
// area), keeping the rest.
[[nodiscard]] Placement move_to(const Placement &placement, Element element, Point centre, const Area &area);

// The fingers on the action layout. Every finger is independent: the stick,
// buttons and the camera work at the same time. A finger landing on a button
// holds it; on the attack cluster (the right thumb's buttons) it may slide
// from one to the next. A finger landing near the stick holds the stick. In
// the swipe area a quick horizontal swipe presses D-pad Left or Right for a
// moment (and again for each further length of swipe); a finger there that
// does not swipe soon turns the camera instead, as a finger landing anywhere
// else free does.
class ActionControls {
public:
    // Swipes: at least this far, in heights of the safe area, within this
    // many milliseconds of landing; each press lasts kSwipePressMs.
    static constexpr float kSwipeDistance = 0.07f;
    static constexpr std::uint64_t kSwipeWindowMs = 280u;
    static constexpr std::uint64_t kSwipePressMs = 130u;

    void set_layout(const ActionLayout &layout, const Area &area, float scale);
    [[nodiscard]] const ActionLayout &layout() const { return layout_; }
    [[nodiscard]] const Area &area() const { return area_; }
    [[nodiscard]] float scale() const { return scale_; }
    [[nodiscard]] const Placed &placed(Element element) const { return placed_[static_cast<std::size_t>(element)]; }

    void finger_down(std::uint64_t id, Point at, std::uint64_t ms);
    void finger_move(std::uint64_t id, Point at, std::uint64_t ms);
    void finger_up(std::uint64_t id, std::uint64_t ms);
    void release_all();

    // PSP buttons held at `ms`, swipes included.
    [[nodiscard]] std::uint16_t buttons(std::uint64_t ms) const;
    // The stick, each axis -1..1, right and down positive.
    [[nodiscard]] Point stick() const;
    [[nodiscard]] Point stick_thumb() const { return stick_thumb_; }
    [[nodiscard]] bool stick_held() const { return stick_finger_.has_value(); }
    [[nodiscard]] bool held(Element element) const;
    // Camera drag since the last take, in pixels.
    [[nodiscard]] Point take_camera_drag();
    // The Pause button was tapped since the last take.
    [[nodiscard]] bool take_menu();
    // Presses that should give haptic feedback since the last take.
    [[nodiscard]] int take_haptics();
    // The swipe last pressed and when, for drawing: -1 left, 1 right, 0 none.
    [[nodiscard]] int swipe_shown(std::uint64_t ms) const;

private:
    enum class Role : std::uint8_t { None, Stick, Button, Camera, Swipe };
    struct Finger {
        std::uint64_t id{};
        bool used{};
        Role role{};
        Element element{Element::Count};
        bool over{};
        Point last;
        Point start;
        std::uint64_t start_ms{};
        bool swiped{};
        Point pending; // camera motion held back while a swipe may still come
    };
    static constexpr std::size_t kFingers = 10u;
    [[nodiscard]] std::optional<Element> button_at(Point at, bool cluster_only) const;
    [[nodiscard]] bool in_swipe_area(Point at) const;
    Finger *find(std::uint64_t id);
    void swipe(int direction, std::uint64_t ms);

    ActionLayout layout_{default_action_layout()};
    Area area_{};
    float scale_{1.0f};
    std::array<Placed, kElements> placed_{};
    std::array<Finger, kFingers> fingers_{};
    std::optional<std::uint64_t> stick_finger_;
    Point stick_thumb_{};
    Point camera_drag_{};
    bool menu_tapped_{};
    int haptics_{};
    std::uint64_t swipe_until_[2]{}; // left, right
};

} // namespace mhp3rd::input::touch
