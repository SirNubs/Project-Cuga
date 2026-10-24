#include "input/gamepad_mapping.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace mhp3rd::input::mapping {
namespace {

constexpr const char *kFields[kTargets] = {
    "a",
    "b",
    "x",
    "y",
    "dpup",
    "dpdown",
    "dpleft",
    "dpright",
    "leftshoulder",
    "rightshoulder",
    "lefttrigger",
    "righttrigger",
    "back",
    "start",
    "leftstick",
    "rightstick",
    "guide",
    "leftx",
    "lefty",
    "rightx",
    "righty",
};

constexpr std::string_view kPlatformField = "platform:";
constexpr std::string_view kHeader =
    "# Gamepad mappings for Yakumo, one per line, in the format of SDL and of the community's\n"
    "# SDL_GameControllerDB. Yakumo reads this file at start. Controls > Controllers adds the\n"
    "# controllers you set up there; mappings from SDL_GameControllerDB can be pasted here too.\n";

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return text;
}

bool is_mapping_line(std::string_view line) {
    line = trim(line);
    return !line.empty() && line.front() != '#' && line.find(',') != std::string_view::npos;
}

// Splits text into lines, each without its line break.
std::vector<std::string_view> split_lines(std::string_view text) {
    std::vector<std::string_view> out;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = end == std::string_view::npos ? text : text.substr(0, end);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        out.push_back(line);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1u);
    }
    return out;
}

bool equal_ignoring_case(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

bool line_matches(std::string_view line, std::string_view guid, std::string_view platform) {
    if (!is_mapping_line(line) || !equal_ignoring_case(guid_of(line), guid)) return false;
    const std::string_view own = platform_of(line);
    return own.empty() || platform.empty() || own == platform;
}

bool parse_int(std::string_view text, int &out) {
    if (text.empty() || text.size() > 4u) return false;
    int value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

} // namespace

bool Element::same_input(const Element &other) const {
    if (kind != other.kind || kind == Kind::None || index != other.index) return false;
    return kind != Kind::Hat || hat_mask == other.hat_mask;
}

std::string text(const Element &e) {
    switch (e.kind) {
    case Element::Kind::Button:
        return "b" + std::to_string(e.index);
    case Element::Kind::Hat:
        return "h" + std::to_string(e.index) + "." + std::to_string(e.hat_mask);
    case Element::Kind::Axis: {
        std::string out = e.range == Element::Range::Positive ? "+" : e.range == Element::Range::Negative ? "-" : "";
        out += "a" + std::to_string(e.index);
        if (e.inverted) out += "~";
        return out;
    }
    case Element::Kind::None:
        break;
    }
    return {};
}

std::string describe(const Element &e) {
    switch (e.kind) {
    case Element::Kind::Button:
        return "Button " + std::to_string(e.index);
    case Element::Kind::Hat: {
        const char *direction = e.hat_mask == 1 ? "up"
            : e.hat_mask == 2                   ? "right"
            : e.hat_mask == 4                   ? "down"
            : e.hat_mask == 8                   ? "left"
                                                : "?";
        return "Hat " + std::to_string(e.index) + " " + direction;
    }
    case Element::Kind::Axis: {
        std::string out = "Axis " + std::to_string(e.index);
        if (e.range == Element::Range::Positive) out += " +";
        if (e.range == Element::Range::Negative) out += " -";
        if (e.inverted) out += " (inverted)";
        return out;
    }
    case Element::Kind::None:
        break;
    }
    return "—";
}

Element parse_element(std::string_view t) {
    t = trim(t);
    Element e;
    if (t.size() >= 2u && t[0] == 'b') {
        if (parse_int(t.substr(1), e.index)) e.kind = Element::Kind::Button;
        return e;
    }
    if (t.size() >= 4u && t[0] == 'h') {
        const std::size_t dot = t.find('.');
        if (dot != std::string_view::npos && parse_int(t.substr(1, dot - 1u), e.index) &&
            parse_int(t.substr(dot + 1u), e.hat_mask))
            e.kind = Element::Kind::Hat;
        return e;
    }
    if (!t.empty() && (t[0] == '+' || t[0] == '-')) {
        e.range = t[0] == '+' ? Element::Range::Positive : Element::Range::Negative;
        t.remove_prefix(1);
    }
    if (!t.empty() && t.back() == '~') {
        e.inverted = true;
        t.remove_suffix(1);
    }
    if (t.size() >= 2u && t[0] == 'a' && parse_int(t.substr(1), e.index)) {
        e.kind = Element::Kind::Axis;
        return e;
    }
    return Element{};
}

const char *field(Target target) {
    return kFields[static_cast<std::size_t>(target)];
}

bool is_axis(Target target) {
    return target == Target::LeftX || target == Target::LeftY || target == Target::RightX || target == Target::RightY;
}

std::size_t count(const Answers &answers) {
    return static_cast<std::size_t>(
        std::count_if(answers.begin(), answers.end(), [](const Element &e) { return !e.empty(); }));
}

std::string clean_name(std::string_view name) {
    std::string out;
    for (char c : name) {
        if (c == ',' || c == '\n' || c == '\r' || c == '\t') c = ' ';
        out += c;
    }
    const std::string_view trimmed = trim(out);
    return trimmed.empty() ? std::string("Controller") : std::string(trimmed);
}

std::string build(std::string_view guid, std::string_view name, const Answers &answers, std::string_view platform) {
    std::string line(trim(guid));
    line += ',';
    line += clean_name(name);
    line += ',';
    for (std::size_t i = 0; i < kTargets; ++i) {
        if (answers[i].empty()) continue;
        line += kFields[i];
        line += ':';
        line += text(answers[i]);
        line += ',';
    }
    if (!platform.empty()) {
        line += kPlatformField;
        line += platform;
        line += ',';
    }
    return line;
}

Answers answers_of(std::string_view line) {
    Answers answers{};
    // Past the GUID and the name.
    for (int skip = 0; skip < 2; ++skip) {
        const std::size_t comma = line.find(',');
        if (comma == std::string_view::npos) return answers;
        line.remove_prefix(comma + 1u);
    }
    while (!line.empty()) {
        const std::size_t comma = line.find(',');
        const std::string_view item = trim(comma == std::string_view::npos ? line : line.substr(0, comma));
        line = comma == std::string_view::npos ? std::string_view{} : line.substr(comma + 1u);
        const std::size_t colon = item.find(':');
        if (colon == std::string_view::npos) continue;
        const std::string_view key = item.substr(0, colon);
        for (std::size_t i = 0; i < kTargets; ++i)
            if (key == kFields[i]) answers[i] = parse_element(item.substr(colon + 1u));
    }
    return answers;
}

std::string_view guid_of(std::string_view line) {
    line = trim(line);
    if (!is_mapping_line(line)) return {};
    return trim(line.substr(0, line.find(',')));
}

std::string_view platform_of(std::string_view line) {
    const std::size_t at = line.find(kPlatformField);
    if (at == std::string_view::npos) return {};
    std::string_view rest = line.substr(at + kPlatformField.size());
    return trim(rest.substr(0, rest.find(',')));
}

std::optional<std::string> find_line(std::string_view text, std::string_view guid, std::string_view platform) {
    for (std::string_view line : split_lines(text))
        if (line_matches(line, guid, platform)) return std::string(trim(line));
    return std::nullopt;
}

std::string without(std::string_view text, std::string_view guid, std::string_view platform, bool *removed) {
    std::string out;
    bool any = false;
    for (std::string_view line : split_lines(text)) {
        if (line_matches(line, guid, platform)) {
            any = true;
            continue;
        }
        out += line;
        out += '\n';
    }
    if (removed != nullptr) *removed = any;
    return out;
}

std::string with_line(std::string_view text, std::string_view line) {
    line = trim(line);
    std::string out = without(text, guid_of(line), platform_of(line));
    if (trim(out).empty()) out = std::string(kHeader);
    out += line;
    out += '\n';
    return out;
}

std::vector<std::string> lines(std::string_view text) {
    std::vector<std::string> out;
    for (std::string_view line : split_lines(text))
        if (is_mapping_line(line)) out.emplace_back(trim(line));
    return out;
}

std::optional<Element> detect(const Snapshot &rest, const Snapshot &now, bool axis_only) {
    if (!axis_only) {
        for (std::size_t i = 0; i < now.buttons.size(); ++i) {
            const bool was = i < rest.buttons.size() && rest.buttons[i];
            if (now.buttons[i] && !was) return Element{Element::Kind::Button, static_cast<int>(i)};
        }
        for (std::size_t i = 0; i < now.hats.size(); ++i) {
            const std::uint8_t was = i < rest.hats.size() ? rest.hats[i] : 0u;
            const std::uint8_t is = now.hats[i];
            // One direction only: a diagonal waits until it settles.
            if (was == 0u && (is == 1u || is == 2u || is == 4u || is == 8u)) {
                Element e{Element::Kind::Hat, static_cast<int>(i)};
                e.hat_mask = is;
                return e;
            }
        }
    }
    // The axis moved furthest from where it rested.
    int best = -1;
    int best_distance = kAxisPress;
    for (std::size_t i = 0; i < now.axes.size(); ++i) {
        const int was = i < rest.axes.size() ? rest.axes[i] : 0;
        if (axis_only && std::abs(was) > kAxisRelease) continue; // a trigger resting at an end
        const int distance = std::abs(static_cast<int>(now.axes[i]) - was);
        if (distance > best_distance) {
            best = static_cast<int>(i);
            best_distance = distance;
        }
    }
    if (best < 0) return std::nullopt;
    const int was = best < static_cast<int>(rest.axes.size()) ? rest.axes[static_cast<std::size_t>(best)] : 0;
    const int moved = static_cast<int>(now.axes[static_cast<std::size_t>(best)]) - was;
    Element e{Element::Kind::Axis, best};
    if (axis_only) {
        e.inverted = moved < 0;
    } else if (std::abs(was) <= kAxisRelease) {
        e.range = moved > 0 ? Element::Range::Positive : Element::Range::Negative;
    } else {
        e.inverted = was > 0;
    }
    return e;
}

bool at_rest(const Snapshot &rest, const Snapshot &now) {
    for (std::size_t i = 0; i < now.buttons.size(); ++i)
        if (now.buttons[i] && !(i < rest.buttons.size() && rest.buttons[i])) return false;
    for (std::size_t i = 0; i < now.hats.size(); ++i)
        if (now.hats[i] != (i < rest.hats.size() ? rest.hats[i] : 0u)) return false;
    for (std::size_t i = 0; i < now.axes.size(); ++i) {
        const int was = i < rest.axes.size() ? rest.axes[i] : 0;
        if (std::abs(static_cast<int>(now.axes[i]) - was) > kAxisRelease) return false;
    }
    return true;
}

} // namespace mhp3rd::input::mapping
