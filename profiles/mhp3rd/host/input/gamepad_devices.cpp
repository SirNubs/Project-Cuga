#include "input/gamepad_devices.hpp"

#include "install/user_data.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <system_error>

namespace mhp3rd::input::devices {
namespace {

struct State {
    std::map<SDL_JoystickID, SDL_Joystick *> open;
    std::set<SDL_JoystickID> logged;
    // The mappings file as last read or written.
    std::string file_text;
    // For each GUID the file maps, the mapping SDL had before it (empty for
    // none), so removing the line can give that back without a restart.
    std::map<std::string, std::string> before;
};

State &state() {
    static State value;
    return value;
}

std::string lower(std::string text) {
    std::transform(
        text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string guid_text(SDL_GUID guid) {
    char text[33]{};
    SDL_GUIDToString(guid, text, sizeof(text));
    return text;
}

std::string take(char *text) {
    std::string out = text != nullptr ? text : "";
    SDL_free(text);
    return out;
}

// What SDL maps this GUID to now, or an empty string.
std::string mapping_for(const std::string &guid) {
    return take(SDL_GetGamepadMappingForGUID(SDL_StringToGUID(guid.c_str())));
}

void remember_before(const std::string &guid) {
    State &s = state();
    const std::string key = lower(guid);
    if (s.before.find(key) == s.before.end()) s.before[key] = mapping_for(guid);
}

std::string read_file(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

// Writes next to the file, then moves it over, so a failed write keeps the
// old file whole.
std::string write_file(const std::filesystem::path &path, const std::string &text) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) return "cannot write " + install::path_to_utf8(temporary);
        out << text;
        if (!out.flush()) return "cannot write " + install::path_to_utf8(temporary);
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return "cannot replace " + install::path_to_utf8(path);
    }
    return {};
}

// Whether SDL_GAMECONTROLLERCONFIG maps this GUID: SDL ranks it above
// anything the program adds, so a saved mapping would not take.
bool environment_maps(const std::string &guid) {
    const char *text = std::getenv("SDL_GAMECONTROLLERCONFIG");
    if (text == nullptr) return false;
    for (const std::string &line : mapping::lines(text))
        if (lower(std::string(mapping::guid_of(line))) == lower(guid)) return true;
    return false;
}

void log_device(const Info &i) {
    char ids[16]{};
    std::snprintf(ids, sizeof(ids), "%04x:%04x", i.vendor, i.product);
    std::cout << "[pad] joystick " << i.id << ": " << (i.name.empty() ? "(no name)" : i.name) << ", USB " << ids
              << ", GUID " << i.guid << ", " << i.buttons << " buttons, " << i.axes << " axes, " << i.hats
              << (i.hats == 1 ? " hat" : " hats");
    if (i.gamepad)
        std::cout << "; gamepad mapping" << (i.saved ? " (gamecontrollerdb.txt)" : "") << ": " << i.mapping;
    else
        std::cout << "; no gamepad mapping: set it up in the menu, Controls > Controllers";
    std::cout << std::endl;
}

} // namespace

std::filesystem::path mappings_file() {
    return install::user_data_directory() / "gamecontrollerdb.txt";
}

void load_mappings() {
    State &s = state();
    const std::filesystem::path path = mappings_file();
    s.file_text = read_file(path);
    const std::string platform = SDL_GetPlatform();
    int lines_here = 0;
    for (const std::string &line : mapping::lines(s.file_text)) {
        if (mapping::platform_of(line) != platform) continue;
        remember_before(std::string(mapping::guid_of(line)));
        ++lines_here;
    }
    if (lines_here > 0) {
        const int added = SDL_AddGamepadMappingsFromFile(install::path_to_utf8(path).c_str());
        if (added < 0)
            std::cout << "[pad] cannot read " << install::path_to_utf8(path) << ": " << SDL_GetError() << "\n";
        else
            std::cout << "[pad] " << lines_here << " mapping(s) for " << platform << " from "
                      << install::path_to_utf8(path) << "\n";
    }
    if (const char *text = std::getenv("SDL_GAMECONTROLLERCONFIG"); text != nullptr && *text != '\0')
        std::cout << "[pad] " << mapping::lines(text).size()
                  << " mapping(s) from SDL_GAMECONTROLLERCONFIG, ahead of any other\n";
}

void added(SDL_JoystickID id) {
    State &s = state();
    if (s.open.find(id) == s.open.end()) {
        SDL_Joystick *handle = SDL_OpenJoystick(id);
        if (handle == nullptr) {
            std::cout << "[pad] cannot open joystick " << id << ": " << SDL_GetError() << "\n";
            return;
        }
        s.open[id] = handle;
    }
    if (!s.logged.insert(id).second) return;
    if (const std::optional<Info> i = info(id)) log_device(*i);
}

void removed(SDL_JoystickID id) {
    State &s = state();
    if (const auto it = s.open.find(id); it != s.open.end()) {
        SDL_CloseJoystick(it->second);
        s.open.erase(it);
    }
    s.logged.erase(id);
}

std::optional<Info> info(SDL_JoystickID id) {
    SDL_Joystick *handle = joystick(id);
    if (handle == nullptr) return std::nullopt;
    Info i;
    i.id = id;
    const char *name = SDL_GetJoystickName(handle);
    i.name = name != nullptr ? name : "";
    i.vendor = SDL_GetJoystickVendor(handle);
    i.product = SDL_GetJoystickProduct(handle);
    i.guid = guid_text(SDL_GetJoystickGUID(handle));
    i.gamepad = SDL_IsGamepad(id);
    if (i.gamepad) i.mapping = take(SDL_GetGamepadMappingForID(id));
    i.saved = mapping::find_line(state().file_text, i.guid, SDL_GetPlatform()).has_value();
    i.buttons = std::max(0, SDL_GetNumJoystickButtons(handle));
    i.axes = std::max(0, SDL_GetNumJoystickAxes(handle));
    i.hats = std::max(0, SDL_GetNumJoystickHats(handle));
    return i;
}

std::vector<Info> list() {
    std::vector<Info> out;
    int count = 0;
    SDL_JoystickID *ids = SDL_GetJoysticks(&count);
    for (int k = 0; ids != nullptr && k < count; ++k) {
        added(ids[k]);
        if (std::optional<Info> i = info(ids[k])) out.push_back(std::move(*i));
    }
    SDL_free(ids);
    return out;
}

SDL_Joystick *joystick(SDL_JoystickID id) {
    const State &s = state();
    const auto it = s.open.find(id);
    return it != s.open.end() ? it->second : nullptr;
}

mapping::Snapshot snapshot(SDL_JoystickID id) {
    mapping::Snapshot out;
    SDL_Joystick *handle = joystick(id);
    if (handle == nullptr) return out;
    const int buttons = std::max(0, SDL_GetNumJoystickButtons(handle));
    const int hats = std::max(0, SDL_GetNumJoystickHats(handle));
    const int axes = std::max(0, SDL_GetNumJoystickAxes(handle));
    for (int k = 0; k < buttons; ++k) out.buttons.push_back(SDL_GetJoystickButton(handle, k));
    for (int k = 0; k < hats; ++k) out.hats.push_back(SDL_GetJoystickHat(handle, k));
    for (int k = 0; k < axes; ++k) out.axes.push_back(SDL_GetJoystickAxis(handle, k));
    return out;
}

bool unmapped_gamepad(const Info &i) {
    return !i.gamepad && i.buttons >= 4 && (i.hats >= 1 || i.axes >= 2);
}

std::string save_mapping(SDL_JoystickID id, const mapping::Answers &answers) {
    const std::optional<Info> i = info(id);
    if (!i) return "The controller is no longer connected.";
    if (mapping::count(answers) == 0u) return "Nothing was recorded.";
    const std::string line = mapping::build(i->guid, i->name, answers, SDL_GetPlatform());
    remember_before(i->guid);
    State &s = state();
    const std::string text = mapping::with_line(s.file_text, line);
    if (std::string error = write_file(mappings_file(), text); !error.empty()) return "Not saved: " + error + ".";
    s.file_text = text;
    std::cout << "[pad] mapping saved for " << i->name << ": " << line << std::endl;
    if (SDL_AddGamepadMapping(line.c_str()) < 0) return std::string("Saved, but SDL refused it: ") + SDL_GetError();
    if (environment_maps(i->guid))
        return "Saved, but SDL_GAMECONTROLLERCONFIG maps this controller too, and it wins while it is set.";
    return {};
}

std::string remove_mapping(SDL_JoystickID id) {
    const std::optional<Info> i = info(id);
    if (!i) return "The controller is no longer connected.";
    State &s = state();
    bool removed_line = false;
    const std::string text = mapping::without(s.file_text, i->guid, SDL_GetPlatform(), &removed_line);
    if (!removed_line) return "The mappings file has no line for this controller.";
    if (std::string error = write_file(mappings_file(), text); !error.empty()) return "Not removed: " + error + ".";
    s.file_text = text;
    const auto before = s.before.find(lower(i->guid));
    const std::string previous = before != s.before.end() ? before->second : std::string{};
    std::cout << "[pad] mapping removed for " << i->name
              << (previous.empty() ? "; in use until the next start" : "; back to SDL's own") << std::endl;
    if (!previous.empty()) {
        SDL_AddGamepadMapping(previous.c_str());
        return {};
    }
    // SDL cannot forget a mapping: clearing one leaves a gamepad with nothing
    // mapped, which would look connected and do nothing.
    return "Removed from the file. The layout stays in use until Yakumo starts again.";
}

} // namespace mhp3rd::input::devices
