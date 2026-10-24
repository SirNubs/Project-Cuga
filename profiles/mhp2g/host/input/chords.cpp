#include "input/chords.hpp"

#include <algorithm>

namespace mhp2g::input {
namespace {

// What a chord the port reads by itself does: nothing the game sees.
constexpr std::uint64_t kReservedEffect = 1ull << 63;

// One chord and everything bound to it.
struct Entry {
    Chord chord;
    std::vector<std::size_t> targets;
    std::uint64_t effect{};
};

std::vector<Entry> entries_of(const Table &table, std::span<const Chord> reserved) {
    std::vector<Entry> entries;
    const auto add = [&](const Chord &chord, std::size_t target, std::uint64_t effect, bool bound) {
        for (Entry &e : entries)
            if (e.chord == chord) {
                if (bound) e.targets.push_back(target);
                e.effect |= effect;
                return;
            }
        Entry e{chord, {}, effect};
        if (bound) e.targets.push_back(target);
        entries.push_back(std::move(e));
    };
    for (std::size_t target = 0; target < table.size(); ++target)
        for (const Chord &c : table.slots(target))
            if (!c.empty() && valid(c)) add(c, target, table.effect(target), true);
    for (const Chord &c : reserved)
        if (valid(c)) add(c, 0u, kReservedEffect, false);
    return entries;
}

} // namespace

void Resolver::reset() {
    *this = Resolver{};
}

PadState Resolver::update(const Table &table, const std::function<bool(Binding)> &held, std::uint64_t now_ms,
    unsigned window_ms, std::span<const Chord> reserved) {
    events_.clear();
    const std::vector<Entry> entries = entries_of(table, reserved);
    const auto entry_of = [&](const Chord &c) -> const Entry * {
        for (const Entry &e : entries)
            if (e.chord == c) return &e;
        return nullptr;
    };

    // What is held now: every input a chord names, and whatever was held.
    std::vector<Binding> inputs;
    const auto note = [&](Binding b) {
        if (std::find(inputs.begin(), inputs.end(), b) == inputs.end()) inputs.push_back(b);
    };
    for (const Entry &e : entries)
        for (const Binding b : e.chord.held()) note(b);
    for (const Held &h : held_) note(h.input);
    std::vector<Held> now_held;
    std::vector<Binding> released;
    for (const Binding b : inputs) {
        const auto was = std::find_if(held_.begin(), held_.end(), [&](const Held &h) { return h.input == b; });
        if (held(b))
            now_held.push_back(was != held_.end() ? *was : Held{b, now_ms, false});
        else if (was != held_.end())
            released.push_back(b);
    }
    held_ = std::move(now_held);
    const auto find_held = [&](Binding b) -> Held * {
        const auto it = std::find_if(held_.begin(), held_.end(), [&](const Held &h) { return h.input == b; });
        return it != held_.end() ? &*it : nullptr;
    };
    const auto all_held = [&](const Chord &c) {
        for (const Binding b : c.held())
            if (find_held(b) == nullptr) return false;
        return true;
    };
    const auto let_go = [&](const Chord &c) {
        for (const Binding b : c.held())
            if (std::find(released.begin(), released.end(), b) != released.end()) return true;
        return false;
    };
    const auto spend = [&](const Chord &c, const Chord &keep) {
        for (const Binding b : c.held())
            if (Held *h = find_held(b); h != nullptr && !keep.contains(b)) h->spent = true;
    };

    // A chord that was waiting and is let go was a tap: it acts now, for a
    // moment.
    for (const Chord &w : waiting_)
        if (let_go(w) && entry_of(w) != nullptr) {
            taps_.push_back({w, now_ms + kMinTapMs});
            events_.push_back({Event::Kind::Tapped, w, now_ms, 0u});
        }
    const std::vector<Chord> was_waiting = std::move(waiting_);
    waiting_.clear();

    // Chords let go. What they grew from acts again if it is still held;
    // anything else left over does nothing until released, so letting go of
    // △ + ○ a little unevenly never presses ○ alone.
    std::vector<Active> kept;
    for (const Active &a : active_) {
        if (entry_of(a.chord) != nullptr && all_held(a.chord)) {
            kept.push_back(a);
            continue;
        }
        events_.push_back({Event::Kind::Released, a.chord, now_ms, 0u});
        if (!a.base.empty() && entry_of(a.base) != nullptr && all_held(a.base)) {
            spend(a.chord, a.base);
            kept.push_back({a.base, {}});
        } else {
            spend(a.chord, {});
        }
    }
    active_ = std::move(kept);

    const auto in_active = [&](Binding b) {
        for (const Active &a : active_)
            if (a.chord.contains(b)) return true;
        return false;
    };
    const auto free_input = [&](Binding b) {
        const Held *h = find_held(b);
        return h != nullptr && !h->spent && !in_active(b);
    };

    // A chord held grows into the longest bound chord that holds it and
    // inputs pressed since: LB held for L, then X, is LB + X.
    for (Active &a : active_) {
        const Entry *best = nullptr;
        for (const Entry &e : entries) {
            if (e.chord.size() <= (best != nullptr ? best->chord.size() : a.chord.size())) continue;
            if (!a.chord.part_of(e.chord)) continue;
            bool ok = true;
            for (const Binding b : e.chord.held()) ok = ok && (a.chord.contains(b) || free_input(b));
            if (ok) best = &e;
        }
        if (best == nullptr) continue;
        const Held *last = nullptr;
        for (const Binding b : best->chord.held())
            if (const Held *h = find_held(b); last == nullptr || h->since > last->since) last = h;
        events_.push_back({Event::Kind::Pressed, best->chord, now_ms, now_ms - last->since});
        a = {best->chord, a.chord};
    }

    // New chords among the inputs left, the longest first.
    std::vector<const Entry *> order;
    for (const Entry &e : entries) {
        bool ok = true;
        for (const Binding b : e.chord.held()) ok = ok && free_input(b);
        if (ok) order.push_back(&e);
    }
    std::stable_sort(
        order.begin(), order.end(), [](const Entry *a, const Entry *b) { return a->chord.size() > b->chord.size(); });
    std::vector<Binding> taken;
    const auto is_taken = [&](Binding b) { return std::find(taken.begin(), taken.end(), b) != taken.end(); };
    for (const Entry *e : order) {
        bool clash = false;
        for (const Binding b : e->chord.held()) clash = clash || is_taken(b);
        if (clash) continue;
        std::uint64_t last = 0u;
        for (const Binding b : e->chord.held()) last = std::max(last, find_held(b)->since);
        const std::uint64_t age = now_ms - last;
        // Wait while a longer chord could still come, if acting now would
        // do harm, until the window has passed since the last input.
        bool wait = false;
        if (window_ms != 0u && age < window_ms) {
            for (const Entry &d : entries) {
                if (d.chord.size() <= e->chord.size() || !e->chord.part_of(d.chord)) continue;
                if (!waits_for(e->effect, d.effect)) continue;
                bool possible = true;
                for (const Binding b : d.chord.held())
                    possible = possible && (e->chord.contains(b) || find_held(b) == nullptr);
                if (possible) {
                    wait = true;
                    break;
                }
            }
        }
        for (const Binding b : e->chord.held()) taken.push_back(b);
        if (wait) {
            waiting_.push_back(e->chord);
            if (std::find(was_waiting.begin(), was_waiting.end(), e->chord) == was_waiting.end())
                events_.push_back({Event::Kind::Waiting, e->chord, now_ms, age});
        } else {
            active_.push_back({e->chord, {}});
            events_.push_back({Event::Kind::Pressed, e->chord, now_ms, age});
        }
    }

    std::erase_if(taps_, [&](const Tap &t) { return t.until <= now_ms; });

    targets_.clear();
    const auto press = [&](const Chord &c) {
        if (const Entry *e = entry_of(c))
            for (const std::size_t t : e->targets)
                if (std::find(targets_.begin(), targets_.end(), t) == targets_.end()) targets_.push_back(t);
    };
    for (const Active &a : active_) press(a.chord);
    for (const Tap &t : taps_) press(t.chord);
    std::sort(targets_.begin(), targets_.end());
    return state_of(table, targets_);
}

std::uint32_t lead_requested(const Table &table, std::span<const std::size_t> targets) {
    std::uint32_t buttons = 0u;
    for (const std::size_t t : targets) {
        if (t >= table.size()) continue;
        const auto effect = static_cast<std::uint32_t>(table.effect(t) & 0xFFFFu);
        if ((effect & kLeadButton) != 0u && (effect & ~kLeadButton) != 0u) buttons |= effect;
    }
    return buttons;
}

std::uint32_t LeadIn::apply(std::uint32_t buttons, std::uint32_t requested, bool advance) {
    const std::uint32_t others = requested & ~kLeadButton;
    // A new request starts the sequence; one playing keeps its buttons.
    if (others != 0u && pending_ == 0u) shown_reads_ = 0u;
    pending_ |= others;
    std::uint32_t out = buttons;
    if (pending_ != 0u) {
        out |= kLeadButton;
        if (held_reads_ < kLeadReads)
            out &= ~pending_;
        else
            out |= pending_;
    }
    if (advance) {
        held_reads_ = (out & kLeadButton) != 0u ? held_reads_ + 1u : 0u;
        if (pending_ != 0u && (out & pending_) == pending_) ++shown_reads_;
        // Done once shown long enough and no longer asked for.
        if (pending_ != 0u && shown_reads_ >= kMinReads && (requested & pending_) == 0u) {
            pending_ = 0u;
            shown_reads_ = 0u;
        }
    }
    return out;
}

PadState state_of(const Table &table, std::span<const std::size_t> targets) {
    PadState pad;
    bool on[kActions]{};
    for (const std::size_t t : targets) {
        if (t >= table.size()) continue;
        pad.buttons |= static_cast<std::uint32_t>(table.effect(t) & 0xFFFFFFFFu);
        if (t < kActions) on[t] = true;
    }
    const auto axis = [&](Action negative, Action positive) {
        return (on[static_cast<std::size_t>(positive)] ? 127 : 0) - (on[static_cast<std::size_t>(negative)] ? 127 : 0);
    };
    pad.stick_x = axis(Action::StickLeft, Action::StickRight);
    pad.stick_y = axis(Action::StickUp, Action::StickDown);
    pad.camera_x = axis(Action::CameraLeft, Action::CameraRight);
    pad.camera_y = axis(Action::CameraUp, Action::CameraDown);
    pad.fast_forward = on[static_cast<std::size_t>(Action::FastForward)];
    pad.screenshot = on[static_cast<std::size_t>(Action::Screenshot)];
    pad.frame_step = on[static_cast<std::size_t>(Action::FrameStep)];
    pad.hide_hud = on[static_cast<std::size_t>(Action::HideHud)];
    pad.lock_on = on[static_cast<std::size_t>(Action::LockOn)];
    return pad;
}

PadState read(const Table &table, const std::function<bool(Binding)> &held) {
    Resolver resolver;
    return resolver.update(table, held, 0u, 0u);
}

PadState read(const Bindings &bindings, const std::function<bool(Binding)> &held) {
    return read(Table{bindings, {}, false}, held);
}

} // namespace mhp2g::input
