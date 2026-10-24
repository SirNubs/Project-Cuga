#pragma once

#include "platform/utf8_path.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>

namespace mhp3rd::install {

// The per-user data directory holds what the installer sets up:
//
//   EBOOT.ELF      the executable prepared from the player's disc image
//   disc.iso       the copied disc image (absent when the image is used in place)
//   settings.ini   where the disc image is, and the player's settings
//
//   ms0/           the memory stick, with the saves
//   and the pipeline caches, logs/, mods/, textures/ and save-backups/.
inline constexpr const char *kExecutableFile = "EBOOT.ELF";
inline constexpr const char *kCopiedImageFile = "disc.iso";
// Android: asks the next start to run the setup, as --install does elsewhere.
inline constexpr const char *kSetupMarkerFile = "setup-requested";
inline constexpr const char *kSettingsFile = "settings.ini";

// Where the data directory is, and why. The first that applies wins:
//
//   CommandLine  --data-dir DIR
//   Environment  MHP3RD_DATA_DIR
//   Portable     data/ next to the executable, when portable.txt or data/ is
//                there, --portable was given or MHP3RD_PORTABLE=1 is set
//   PerUser      SDL_GetPrefPath("Yakumo", "MHP3rd")
//
// MHP3RD_PORTABLE=0 turns the portable check off.
enum class DataSource { CommandLine, Environment, Portable, PerUser };

// A portable copy: a file of this name next to the executable, or the data
// folder itself, keeps all of Yakumo's data in that folder.
inline constexpr const char *kPortableMarkerFile = "portable.txt";
inline constexpr const char *kPortableDataFolder = "data";

struct DataDirectory {
    std::filesystem::path path;
    DataSource source = DataSource::PerUser;
};

// Command-line choices, made before the first call below.
void set_data_directory_override(std::filesystem::path directory); // --data-dir
void set_portable_requested();                                     // --portable

// The data directory for this run, decided once and then kept.
[[nodiscard]] const DataDirectory &data_directory();
// data_directory().path.
[[nodiscard]] std::filesystem::path user_data_directory();

// The rules above, without the process's own state, for tests: executable_dir
// is where the executable is, portable whether --portable or MHP3RD_PORTABLE=1
// asked for portable mode, and no_portable whether MHP3RD_PORTABLE=0 turned
// the check off. Empty when portable mode is not on there.
[[nodiscard]] std::filesystem::path portable_data_directory(
    const std::filesystem::path &executable_dir, bool portable, bool no_portable);
// SDL_GetPrefPath("Yakumo", "MHP3rd"), or the same location computed by hand in
// a build without SDL: where an installed copy keeps its data. SDL creates the
// directory if it does not exist yet.
[[nodiscard]] std::filesystem::path per_user_data_directory();
// The same location worked out from the environment, without creating it;
// empty when it cannot be told.
[[nodiscard]] std::filesystem::path expected_per_user_data_directory();

// Creates directory if needed and writes and removes a small file in it.
// Empty when that worked; otherwise what went wrong, written for the player.
[[nodiscard]] std::string check_writable(const std::filesystem::path &directory);

// Whether directory holds anything Yakumo keeps: settings, an installation or
// saves.
[[nodiscard]] bool has_user_data(const std::filesystem::path &directory);

// Copies everything in from into to, recursively. Never overwrites a file that
// already exists in to and never changes from. progress, when given, gets the
// bytes copied so far and the total; it may throw to stop the copy. A copy
// that stops, by an error or by progress throwing, removes the files it had
// copied, so to is left as it was apart from empty folders. Returns the number of files copied;
// throws std::filesystem::filesystem_error when a file cannot be copied.
using CopyProgressFn = std::function<void(std::uint64_t done, std::uint64_t total)>;
std::uint64_t copy_user_data(
    const std::filesystem::path &from, const std::filesystem::path &to, const CopyProgressFn &progress = {});

// Every key=value line of settings.ini. The installer owns disc_image; the
// player's settings (host/settings) keep their keys next to it, and writing
// one never drops the others.
using SettingsEntries = std::map<std::string, std::string>;

[[nodiscard]] SettingsEntries read_settings_file(const std::filesystem::path &data_dir);
// Replaces settings.ini with `entries`, creating data_dir if needed.
void write_settings_file(const std::filesystem::path &data_dir, const SettingsEntries &entries);

struct UserSettings {
    // Disc image to read. Relative paths are relative to the data directory.
    std::filesystem::path disc_image = kCopiedImageFile;
};

[[nodiscard]] UserSettings load_settings(const std::filesystem::path &data_dir);
void save_settings(const std::filesystem::path &data_dir, const UserSettings &settings);

struct Installation {
    std::filesystem::path executable;
    std::filesystem::path disc_image; // absolute
    bool image_copied{};              // disc image lives in the data directory
};

// The installation in data_dir, if the installer has completed there. The disc
// image is not checked for existence: callers report a missing image.
[[nodiscard]] std::optional<Installation> find_installation(const std::filesystem::path &data_dir);

// UTF-8 conversions for paths shown in dialogs, stored in settings or received
// from SDL (platform/utf8_path.hpp).
using mhp3rd::path_from_utf8;
using mhp3rd::path_to_utf8;

} // namespace mhp3rd::install
