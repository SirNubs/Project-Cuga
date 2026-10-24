#pragma once

#include "input/gamepad_mapping.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Every controller SDL sees, gamepad or plain joystick (#147): what each one
// is, the mapping that makes it a gamepad, and the mappings file.
//
// SDL reports a device as a gamepad only when it has a mapping for it; the
// game and the menu read gamepads alone. Mappings come, in rising priority,
// from SDL's own database, from gamecontrollerdb.txt in the data directory
// (what Controls > Controllers saves, or lines pasted from the community's
// SDL_GameControllerDB), and from the SDL_GAMECONTROLLERCONFIG environment
// variable, which SDL reads itself.
//
// Each joystick is logged once when it appears, with "[pad]": its name, USB
// ids, GUID, buttons, axes and hats, and its mapping or the lack of one.
namespace mhp2g::input::devices {

struct Info {
    SDL_JoystickID id{};
    std::string name;
    std::uint16_t vendor{};
    std::uint16_t product{};
    std::string guid;
    bool gamepad{};      // SDL has a mapping: the game and the menu can read it
    std::string mapping; // that mapping, as SDL has it
    bool saved{};        // gamecontrollerdb.txt in the data directory has one for it
    int buttons{};
    int axes{};
    int hats{};
};

// gamecontrollerdb.txt in the data directory.
[[nodiscard]] std::filesystem::path mappings_file();

// Reads the mappings file into SDL. Called once the gamepad subsystem is up.
void load_mappings();

// SDL_EVENT_JOYSTICK_ADDED and _REMOVED: keeps every joystick open, so its
// state can be shown, and logs a new one. Seeing one twice is harmless.
void added(SDL_JoystickID id);
void removed(SDL_JoystickID id);

// Every joystick connected, in SDL's order.
[[nodiscard]] std::vector<Info> list();
[[nodiscard]] std::optional<Info> info(SDL_JoystickID id);
// The open joystick, or null.
[[nodiscard]] SDL_Joystick *joystick(SDL_JoystickID id);
// Its buttons, hats and axes now.
[[nodiscard]] mapping::Snapshot snapshot(SDL_JoystickID id);

// A joystick without a mapping that has what a gamepad has: a few buttons and
// a D-pad or a stick. Such a device does nothing until it is set up, and the
// player is told so.
[[nodiscard]] bool unmapped_gamepad(const Info &info);

// Saves the answers of the setup as this device's mapping in the mappings
// file and applies it at once: the device is a gamepad from then on, in the
// game and in the menu. An empty string, or what went wrong.
[[nodiscard]] std::string save_mapping(SDL_JoystickID id, const mapping::Answers &answers);
// Takes this device's line out of the mappings file and gives it back SDL's
// own mapping, if it had one; otherwise the layout stays in use until the
// next start, and the message says so.
[[nodiscard]] std::string remove_mapping(SDL_JoystickID id);

} // namespace mhp2g::input::devices
