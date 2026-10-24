#pragma once

#include "input/bindings.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

// Matching the held inputs of one device to its chords over time (#198).
//
// The longest bound chord among the held inputs wins, and its inputs do
// nothing else; what is left is matched the same way. A chord that could
// still grow into a longer bound one waits a short window (the chord window,
// ~50 ms) for the rest, but only when acting at once would do harm
// (waits_for): an input in no chord, or one whose longer chord does what it
// does and more (LB in LB + X for L + □), acts at once. An input tapped and
// released inside the window still presses its action, for a moment, when
// it is released. Nothing here needs SDL; the time is the caller's.
namespace mhp2g::input {

// What the window adds when a tap is released: at least this long pressed,
// so a game reading 30 times a second sees it.
inline constexpr unsigned kMinTapMs = 40u;
inline constexpr unsigned kDefaultChordWindowMs = 50u;
inline constexpr unsigned kMaxChordWindowMs = 200u;

class Resolver {
public:
    // What the held inputs press at `now_ms`, a steady clock in
    // milliseconds. `window_ms` 0 turns the window off. `reserved` are chords
    // the port reads by itself, such as L3 + R3 for the menu: they win like
    // any chord and press nothing.
    PadState update(const Table &table, const std::function<bool(Binding)> &held, std::uint64_t now_ms,
        unsigned window_ms, std::span<const Chord> reserved = {});
    // Forgets everything held, for when the device goes away.
    void reset();

    // What an update decided, for MHP2G_TRACE_PAD.
    struct Event {
        enum class Kind : std::uint8_t { Pressed, Waiting, Tapped, Released };
        Kind kind{};
        Chord chord;
        std::uint64_t at_ms{};
        std::uint64_t waited_ms{}; // since its last input went down
    };
    [[nodiscard]] const std::vector<Event> &events() const { return events_; }
    // The targets pressing now, in Table order.
    [[nodiscard]] const std::vector<std::size_t> &targets() const { return targets_; }

private:
    struct Held {
        Binding input{};
        std::uint64_t since{};
        bool spent{}; // left over from a chord that let go: nothing until released
    };
    struct Active {
        Chord chord;
        Chord base; // what it grew from, which acts again if it is let go
    };
    struct Tap {
        Chord chord;
        std::uint64_t until{};
    };
    std::vector<Held> held_;
    std::vector<Active> active_;
    std::vector<Chord> waiting_;
    std::vector<Tap> taps_;
    std::vector<Event> events_;
    std::vector<std::size_t> targets_;
};

// L before the rest (#198). Traced in a quest: the game scrolls the item
// bar with □ or ○ only once L has been held on its own for 8 of its frames
// (the bar opens while L is held); L and □ in the same frame do nothing, and
// □ after fewer frames of L uses the item. So an action that presses L with
// other buttons, such as Item left, sends L alone for kLeadReads reads of
// the pad first, then the rest for at least kMinReads, even for a tap.
// Anything else passes through unchanged, and L held already counts.
inline constexpr unsigned kLeadReads = 10u;
inline constexpr unsigned kMinReads = 3u;
inline constexpr std::uint32_t kLeadButton = 0x0100u; // L
class LeadIn {
public:
    // `buttons`: what the bindings and everything else press now.
    // `requested`: of those, the buttons of actions that press L with
    // others. `advance`: this is a read the game sees, which counts.
    std::uint32_t apply(std::uint32_t buttons, std::uint32_t requested, bool advance);
    void reset() { *this = LeadIn{}; }

private:
    unsigned held_reads_{};   // reads in a row the game has seen L
    std::uint32_t pending_{}; // the others, waiting for L or being shown
    unsigned shown_reads_{};  // reads the others have been shown
};
// Of the targets pressing, the buttons of those that press L with others.
[[nodiscard]] std::uint32_t lead_requested(const Table &table, std::span<const std::size_t> targets);

// The PSP state of the targets pressing, as read() gives it.
[[nodiscard]] PadState state_of(const Table &table, std::span<const std::size_t> targets);

} // namespace mhp2g::input
