#include "ui/input_script.hpp"

#include "ui/layer.hpp"

#include "gpu/vulkan_renderer.hpp"
#include "platform/utf8_path.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace mhp3rd::ui::script {
namespace {

// Frames a scripted button stays down: ImGui samples the pad once per frame.
constexpr std::uint64_t kHoldFrames = 4u;

struct Step {
    std::uint64_t frame{};
    std::string action;
    std::string argument;
};

struct State {
    bool attached{};
    std::deque<Step> steps;
    std::uint64_t frame{};
    SDL_Joystick *pad{};
    // Releases due later: frame, key or pad buttons.
    struct Release {
        std::uint64_t frame{};
        SDL_Keycode key{};
        std::vector<SDL_GamepadButton> buttons;
        std::uint8_t mouse_button{};
        // A test joystick's button (joy_button) or hat (joy_hat) to let go.
        SDL_Joystick *joystick{};
        int joy_button{-1};
        bool joy_hat{};
    };
    // Test joysticks without a mapping (`joy`), by the script's number.
    std::map<int, SDL_Joystick *> joysticks;
    std::vector<Release> releases;
    // Strings handed to SDL events must outlive them.
    std::deque<std::string> strings;
    // MHP3RD_INPUT_LIVE: a file whose appended lines are read as they come.
    std::filesystem::path live_path;
    std::streamoff live_offset{};
};

State &state() {
    static State value;
    return value;
}

std::string trim(const std::string &text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t") - first + 1u);
}

void push_key(SDL_Keycode key, bool down) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.timestamp = SDL_GetTicksNS();
    event.key.windowID = SDL_GetWindowID(Layer::get().renderer().window());
    event.key.key = key;
    event.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
    event.key.down = down;
    SDL_PushEvent(&event);
    // A pushed event does not change SDL's keyboard snapshot, which the game's
    // bindings read, so the key is handed to the renderer as well.
    Layer::get().renderer().set_scripted_key(static_cast<int>(event.key.scancode), down);
}

void push_mouse_button(std::uint8_t button, bool down) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.timestamp = SDL_GetTicksNS();
    event.button.windowID = SDL_GetWindowID(Layer::get().renderer().window());
    event.button.which = gpu::kScriptedMouse;
    event.button.button = button;
    event.button.down = down;
    event.button.clicks = 1u;
    SDL_PushEvent(&event);
}

// "NAME N": the name, and N frames if the last word is a number.
std::pair<std::string, std::uint64_t> name_and_frames(const std::string &argument, std::uint64_t fallback) {
    const auto space = argument.find_last_of(' ');
    if (space == std::string::npos) return {argument, fallback};
    const std::string last = argument.substr(space + 1u);
    if (last.empty() || last.find_first_not_of("0123456789") != std::string::npos) return {argument, fallback};
    return {argument.substr(0, space), std::max<std::uint64_t>(1u, std::strtoull(last.c_str(), nullptr, 10))};
}

bool mouse_step(const std::string &action) {
    return action == "mouse" || action == "click";
}

// The virtual touch screen's device id.
constexpr SDL_TouchID kScriptTouch = 0x59414bu;

void push_finger(Uint32 type, std::uint64_t finger, float x, float y) {
    SDL_Event event{};
    event.type = type;
    event.tfinger.timestamp = SDL_GetTicksNS();
    event.tfinger.touchID = kScriptTouch;
    event.tfinger.fingerID = static_cast<SDL_FingerID>(finger);
    event.tfinger.x = x;
    event.tfinger.y = y;
    event.tfinger.pressure = type == SDL_EVENT_FINGER_UP ? 0.0f : 1.0f;
    event.tfinger.windowID = SDL_GetWindowID(Layer::get().renderer().window());
    SDL_PushEvent(&event);
}

// The pointer as a touch moves it: absolute, in window coordinates.
void push_pointer(float x, float y) {
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(Layer::get().renderer().window(), &width, &height);
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.timestamp = SDL_GetTicksNS();
    event.motion.windowID = SDL_GetWindowID(Layer::get().renderer().window());
    event.motion.which = SDL_TOUCH_MOUSEID;
    event.motion.x = x * static_cast<float>(width);
    event.motion.y = y * static_cast<float>(height);
    SDL_PushEvent(&event);
}

void push_pointer_button(bool down) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.timestamp = SDL_GetTicksNS();
    event.button.windowID = SDL_GetWindowID(Layer::get().renderer().window());
    event.button.which = SDL_TOUCH_MOUSEID;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = down;
    event.button.clicks = 1u;
    float x = 0.0f;
    float y = 0.0f;
    SDL_GetMouseState(&x, &y);
    event.button.x = x;
    event.button.y = y;
    SDL_PushEvent(&event);
}

// Numbers after an action's first word.
std::vector<float> numbers(const std::string &text) {
    std::vector<float> values;
    std::stringstream in(text);
    float value = 0.0f;
    while (in >> value) values.push_back(value);
    return values;
}

void add_step(std::uint64_t frame, std::string action, std::string argument);

// `joy K attach [NAME]`, `joy K button N [F]`, `joy K hat MASK [F]`,
// `joy K axis N VALUE`, `joy K detach`: a joystick SDL has no mapping for,
// like a controller it does not know (#147).
void run_joystick(const std::string &argument) {
    State &s = state();
    std::stringstream in(argument);
    int number = 0;
    std::string what;
    in >> number >> what;
    const auto found = s.joysticks.find(number);
    SDL_Joystick *joystick = found != s.joysticks.end() ? found->second : nullptr;
    if (what == "attach") {
        if (joystick != nullptr) return;
        std::string name;
        std::getline(in, name);
        name = trim(name);
        if (name.empty()) name = "Yakumo test joystick " + std::to_string(number);
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        // No gamepad type: SDL then gives it no mapping of its own. A PS2 pad
        // on a USB adapter: 12 buttons, a hat, two sticks.
        desc.type = SDL_JOYSTICK_TYPE_UNKNOWN;
        desc.vendor_id = 0x0810u;
        desc.product_id = 0x0001u;
        desc.nbuttons = 12u;
        desc.naxes = 4u;
        desc.nhats = 1u;
        s.strings.push_back(name);
        desc.name = s.strings.back().c_str();
        const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
        if (id == 0) {
            std::cout << "[script] cannot attach a test joystick: " << SDL_GetError() << std::endl;
            return;
        }
        s.joysticks[number] = SDL_OpenJoystick(id);
        return;
    }
    if (joystick == nullptr) {
        std::cout << "[script] no test joystick " << number << std::endl;
        return;
    }
    if (what == "detach") {
        const SDL_JoystickID id = SDL_GetJoystickID(joystick);
        SDL_CloseJoystick(joystick);
        SDL_DetachVirtualJoystick(id);
        s.joysticks.erase(number);
        return;
    }
    int value = 0;
    in >> value;
    if (what == "axis") {
        float position = 0.0f;
        in >> position;
        SDL_SetJoystickVirtualAxis(joystick, value, static_cast<Sint16>(std::clamp(position, -1.0f, 1.0f) * 32767.0f));
        return;
    }
    std::uint64_t frames = kHoldFrames;
    if (std::uint64_t given = 0; in >> given) frames = std::max<std::uint64_t>(1u, given);
    State::Release release{s.frame + frames, SDLK_UNKNOWN, {}};
    release.joystick = joystick;
    if (what == "button") {
        SDL_SetJoystickVirtualButton(joystick, value, true);
        release.joy_button = value;
    } else if (what == "hat") {
        SDL_SetJoystickVirtualHat(joystick, 0, static_cast<Uint8>(value));
        release.joy_hat = true;
    } else {
        std::cout << "[script] unknown joystick action " << what << std::endl;
        return;
    }
    s.releases.push_back(release);
}

void run(const Step &due) {
    // A copy: the steps some actions add reorder the list `due` is in.
    const Step step = due;
    State &s = state();
    std::cout << "[script] frame " << s.frame << ": " << step.action << " " << step.argument << std::endl;
    if (step.action == "key") {
        const auto [name, frames] = name_and_frames(step.argument, kHoldFrames);
        const SDL_Keycode key = SDL_GetKeyFromName(name.c_str());
        if (key == SDLK_UNKNOWN) {
            std::cout << "[script] unknown key " << step.argument << std::endl;
            return;
        }
        push_key(key, true);
        s.releases.push_back({s.frame + frames, key, {}});
    } else if (step.action == "mouse") {
        float dx = 0.0f;
        float dy = 0.0f;
        std::stringstream(step.argument) >> dx >> dy;
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.timestamp = SDL_GetTicksNS();
        event.motion.windowID = SDL_GetWindowID(Layer::get().renderer().window());
        event.motion.which = gpu::kScriptedMouse;
        event.motion.xrel = dx;
        event.motion.yrel = dy;
        SDL_PushEvent(&event);
    } else if (step.action == "click") {
        const auto [name, frames] = name_and_frames(step.argument, kHoldFrames);
        static const std::pair<const char *, std::uint8_t> kButtons[] = {{"left", SDL_BUTTON_LEFT},
            {"middle", SDL_BUTTON_MIDDLE}, {"right", SDL_BUTTON_RIGHT}, {"x1", SDL_BUTTON_X1}, {"x2", SDL_BUTTON_X2}};
        std::uint8_t button = 0u;
        for (const auto &[button_name, value] : kButtons)
            if (name == button_name) button = value;
        if (button == 0u) {
            std::cout << "[script] unknown mouse button " << step.argument << std::endl;
            return;
        }
        push_mouse_button(button, true);
        s.releases.push_back({s.frame + frames, SDLK_UNKNOWN, {}, button});
    } else if (step.action == "pad") {
        if (s.pad == nullptr) return;
        const auto [held, frames] = name_and_frames(step.argument, kHoldFrames);
        std::vector<SDL_GamepadButton> buttons;
        std::stringstream names(held);
        std::string name;
        while (std::getline(names, name, '+')) {
            const SDL_GamepadButton button = SDL_GetGamepadButtonFromString(name.c_str());
            if (button == SDL_GAMEPAD_BUTTON_INVALID) {
                std::cout << "[script] unknown button " << name << std::endl;
                continue;
            }
            SDL_SetJoystickVirtualButton(s.pad, button, true);
            buttons.push_back(button);
        }
        s.releases.push_back({s.frame + frames, SDLK_UNKNOWN, buttons});
    } else if (step.action == "axis") {
        if (s.pad == nullptr) return;
        const auto space = step.argument.find(' ');
        const SDL_GamepadAxis axis = SDL_GetGamepadAxisFromString(step.argument.substr(0, space).c_str());
        const float value =
            space == std::string::npos ? 0.0f : std::strtof(step.argument.c_str() + space + 1u, nullptr);
        if (axis == SDL_GAMEPAD_AXIS_INVALID) {
            std::cout << "[script] unknown axis " << step.argument << std::endl;
            return;
        }
        SDL_SetJoystickVirtualAxis(s.pad, axis, static_cast<Sint16>(std::clamp(value, -1.0f, 1.0f) * 32767.0f));
    } else if (step.action == "joy") {
        run_joystick(step.argument);
    } else if (step.action == "finger") {
        std::stringstream in(step.argument);
        std::uint64_t id = 0;
        std::string what;
        float x = 0.0f;
        float y = 0.0f;
        in >> id >> what >> x >> y;
        const Uint32 type = what == "down" ? SDL_EVENT_FINGER_DOWN
            : what == "move"               ? SDL_EVENT_FINGER_MOTION
                                           : SDL_EVENT_FINGER_UP;
        push_finger(type, id + 1u, x, y);
    } else if (step.action == "hold" || step.action == "swipe") {
        const std::vector<float> v = numbers(step.argument);
        const bool swipe = step.action == "swipe";
        if (v.size() < (swipe ? 5u : 3u)) {
            std::cout << "[script] " << step.action << " needs a finger and a position" << std::endl;
            return;
        }
        const std::string id = std::to_string(static_cast<std::uint64_t>(v[0]));
        const auto frames =
            static_cast<std::uint64_t>(v.size() > (swipe ? 5u : 3u) ? v[swipe ? 5 : 3] : (swipe ? 8 : 4));
        push_finger(SDL_EVENT_FINGER_DOWN, static_cast<std::uint64_t>(v[0]) + 1u, v[1], v[2]);
        for (std::uint64_t k = 1; swipe && k <= frames; ++k) {
            const float t = static_cast<float>(k) / static_cast<float>(frames);
            add_step(s.frame + k, "finger",
                id + " move " + std::to_string(v[1] + (v[3] - v[1]) * t) + " " +
                    std::to_string(v[2] + (v[4] - v[2]) * t));
        }
        add_step(s.frame + frames + 1u, "finger", id + " up");
    } else if (step.action == "drag") {
        const std::vector<float> v = numbers(step.argument);
        if (v.size() < 4u) {
            std::cout << "[script] drag needs two positions" << std::endl;
            return;
        }
        const auto frames = static_cast<std::uint64_t>(v.size() > 4u ? v[4] : 8);
        push_pointer(v[0], v[1]);
        add_step(s.frame + 1u, "pointer", "down");
        for (std::uint64_t k = 1; k <= frames; ++k) {
            const float t = static_cast<float>(k) / static_cast<float>(frames);
            add_step(s.frame + 1u + k, "pointer",
                std::to_string(v[0] + (v[2] - v[0]) * t) + " " + std::to_string(v[1] + (v[3] - v[1]) * t));
        }
        add_step(s.frame + frames + 3u, "pointer", "up");
    } else if (step.action == "pointer") {
        if (step.argument == "down" || step.argument == "up") {
            push_pointer_button(step.argument == "down");
        } else {
            const std::vector<float> v = numbers(step.argument);
            if (v.size() >= 2u) push_pointer(v[0], v[1]);
        }
    } else if (step.action == "text") {
        SDL_Event event{};
        event.type = SDL_EVENT_TEXT_INPUT;
        event.text.windowID = SDL_GetWindowID(Layer::get().renderer().window());
        event.text.text = s.strings.emplace_back(step.argument).c_str();
        SDL_PushEvent(&event);
    } else if (step.action == "drop") {
        SDL_Event event{};
        event.type = SDL_EVENT_DROP_FILE;
        event.drop.windowID = SDL_GetWindowID(Layer::get().renderer().window());
        event.drop.data = s.strings.emplace_back(step.argument).c_str();
        SDL_PushEvent(&event);
    } else if (step.action == "shot") {
        std::filesystem::path dir = environment_path("MHP3RD_SCREENSHOT_DIR");
        if (dir.empty()) dir = ".";
        const std::string name = step.argument.empty() ? "frame_" + std::to_string(s.frame) : step.argument;
        Layer::get().renderer().capture_window(dir / path_from_utf8(name + ".bmp"));
    } else if (step.action == "quit") {
        SDL_Event event{};
        event.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&event);
    } else {
        std::cout << "[script] unknown action " << step.action << std::endl;
    }
}

void add_step(std::uint64_t frame, std::string action, std::string argument) {
    State &s = state();
    s.steps.push_back({frame, std::move(action), std::move(argument)});
    std::stable_sort(s.steps.begin(), s.steps.end(), [](const Step &a, const Step &b) { return a.frame < b.frame; });
}

// Parses `frame:action argument`; `base` is added to the frame.
bool parse_step(std::string item, std::uint64_t base, Step &step) {
    item = trim(item);
    const auto colon = item.find(':');
    if (item.empty() || colon == std::string::npos) return false;
    step.frame = base + std::strtoull(item.substr(0, colon).c_str(), nullptr, 10);
    const std::string rest = trim(item.substr(colon + 1u));
    const auto space = rest.find(' ');
    step.action = rest.substr(0, space);
    step.argument = space == std::string::npos ? std::string{} : trim(rest.substr(space + 1u));
    return true;
}

void sort_steps(State &s) {
    std::stable_sort(s.steps.begin(), s.steps.end(), [](const Step &a, const Step &b) { return a.frame < b.frame; });
}

// Reads the lines appended to the live file since the last call. Their frames
// count from now, so a line `30:pad a` presses ○ half a second after it is read.
void read_live(State &s) {
    std::ifstream file(s.live_path, std::ios::binary);
    if (!file) return;
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < s.live_offset) s.live_offset = 0; // the file was replaced
    if (size == s.live_offset) return;
    file.seekg(s.live_offset);
    std::string line;
    bool added = false;
    while (std::getline(file, line)) {
        if (file.eof()) break; // an incomplete last line: read it next time
        s.live_offset = file.tellg();
        Step step;
        if (parse_step(line, s.frame, step)) {
            s.steps.push_back(step);
            added = true;
        }
    }
    if (added) sort_steps(s);
}

void attach_pad(State &s);

} // namespace

void attach() {
    State &s = state();
    if (s.attached) return;
    s.attached = true;
    bool uses_pad = false;
    bool uses_mouse = false;
    if (std::filesystem::path live = environment_path("MHP3RD_INPUT_LIVE"); !live.empty()) {
        s.live_path = std::move(live);
        // Only what is appended after start-up counts.
        std::ifstream file(s.live_path, std::ios::binary | std::ios::ate);
        if (file) s.live_offset = file.tellg();
        uses_pad = true;
        uses_mouse = true;
        std::cout << "[script] reading live input from " << path_to_utf8(s.live_path) << std::endl;
    }
    // UTF-8: typed text and dropped paths go to SDL as they are.
    if (const std::optional<std::string> text = environment_utf8("MHP3RD_INPUT_SCRIPT")) {
        std::stringstream list(*text);
        std::string item;
        while (std::getline(list, item, ';')) {
            Step step;
            if (!parse_step(item, 0u, step)) continue;
            uses_pad = uses_pad || step.action == "pad" || step.action == "axis";
            uses_mouse = uses_mouse || mouse_step(step.action);
            s.steps.push_back(step);
        }
        sort_steps(s);
        std::cout << "[script] " << s.steps.size() << " steps" << std::endl;
    }
    if (uses_pad) attach_pad(s);
    if (uses_mouse) Layer::get().renderer().set_scripted_input(true);
}

namespace {

void attach_pad(State &s) {
    // Scripted runs usually go on in the background, where SDL would
    // otherwise ignore the pad.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1u;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1u;
    // The renderer gives the game this pad over a real one by its name.
    desc.name = "Yakumo input script";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    if (id == 0) {
        std::cout << "[script] cannot attach a virtual gamepad: " << SDL_GetError() << std::endl;
        return;
    }
    s.pad = SDL_OpenJoystick(id);
}

} // namespace

void tick() {
    State &s = state();
    if (s.steps.empty() && s.releases.empty() && s.live_path.empty()) return;
    ++s.frame;
    if (!s.live_path.empty() && s.frame % 10u == 0u) read_live(s);
    for (auto it = s.releases.begin(); it != s.releases.end();) {
        if (it->frame > s.frame) {
            ++it;
            continue;
        }
        if (it->key != SDLK_UNKNOWN) push_key(it->key, false);
        if (it->mouse_button != 0u) push_mouse_button(it->mouse_button, false);
        for (SDL_GamepadButton button : it->buttons) SDL_SetJoystickVirtualButton(s.pad, button, false);
        if (it->joystick != nullptr && it->joy_button >= 0)
            SDL_SetJoystickVirtualButton(it->joystick, it->joy_button, false);
        if (it->joystick != nullptr && it->joy_hat) SDL_SetJoystickVirtualHat(it->joystick, 0, SDL_HAT_CENTERED);
        it = s.releases.erase(it);
    }
    while (!s.steps.empty() && s.steps.front().frame <= s.frame) {
        run(s.steps.front());
        s.steps.pop_front();
    }
}

} // namespace mhp3rd::ui::script
