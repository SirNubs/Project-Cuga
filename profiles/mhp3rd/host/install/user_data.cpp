#include "install/user_data.hpp"

#include "app_paths.hpp"
#include "psprecomp/common.hpp"

#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if defined(MHP3RD_HAS_SDL)
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>
#endif

namespace mhp3rd::install {
namespace {

constexpr const char *kOrganization = "Yakumo";
constexpr const char *kApplication = "MHP3rd";

std::string trim(const std::string &text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1u);
}

std::filesystem::path &command_line_directory() {
    static std::filesystem::path directory;
    return directory;
}

bool &portable_requested() {
    static bool requested = false;
    return requested;
}

} // namespace

std::filesystem::path per_user_data_directory() {
#if defined(MHP3RD_HAS_SDL)
    char *pref = SDL_GetPrefPath(kOrganization, kApplication);
    if (pref == nullptr)
        throw psprecomp::Error(std::string("Cannot determine the user data directory: ") + SDL_GetError());
    std::filesystem::path result = path_from_utf8(pref);
    SDL_free(pref);
    return result;
#else
    std::filesystem::path result = expected_per_user_data_directory();
    if (result.empty()) throw psprecomp::Error("Cannot determine the user data directory; set MHP3RD_DATA_DIR");
    return result;
#endif
}

std::filesystem::path expected_per_user_data_directory() {
    // The locations SDL_GetPrefPath uses, so builds with and without SDL agree.
    std::filesystem::path base;
#if defined(_WIN32)
    base = environment_path("APPDATA");
#elif defined(__APPLE__)
    if (const auto home = environment_path("HOME"); !home.empty()) base = home / "Library" / "Application Support";
#else
    base = environment_path("XDG_DATA_HOME");
    if (const auto home = environment_path("HOME"); base.empty() && !home.empty()) base = home / ".local" / "share";
#endif
    if (base.empty()) return {};
    return base / kOrganization / kApplication;
}

std::filesystem::path portable_data_directory(
    const std::filesystem::path &executable_dir, bool portable, bool no_portable) {
    if (executable_dir.empty() || no_portable) return {};
    const std::filesystem::path data = executable_dir / kPortableDataFolder;
    std::error_code ec;
    if (portable || std::filesystem::exists(executable_dir / kPortableMarkerFile, ec) ||
        std::filesystem::is_directory(data, ec))
        return data;
    return {};
}

void set_data_directory_override(std::filesystem::path directory) {
    command_line_directory() = std::move(directory);
}

void set_portable_requested() {
    portable_requested() = true;
}

const DataDirectory &data_directory() {
    static const DataDirectory resolved = [] {
        DataDirectory result;
        if (!command_line_directory().empty()) {
            result.path = command_line_directory();
            result.source = DataSource::CommandLine;
            return result;
        }
        if (std::filesystem::path dir = environment_path("MHP3RD_DATA_DIR"); !dir.empty()) {
            result.path = std::move(dir);
            result.source = DataSource::Environment;
            return result;
        }
#if !defined(MHP3RD_ANDROID_APP)
        const std::optional<std::string> portable_env = environment_utf8("MHP3RD_PORTABLE");
        const bool env_on = portable_env == "1";
        const bool env_off = portable_env == "0";
        const std::filesystem::path portable =
            portable_data_directory(executable_directory(), portable_requested() || env_on, env_off);
        if (!portable.empty()) {
            result.path = portable;
            result.source = DataSource::Portable;
            return result;
        }
#endif
        result.path = per_user_data_directory();
        result.source = DataSource::PerUser;
        return result;
    }();
    return resolved;
}

std::filesystem::path user_data_directory() {
    return data_directory().path;
}

std::string check_writable(const std::filesystem::path &directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) return "Yakumo cannot create its data folder:\n" + path_to_utf8(directory) + "\n\n" + ec.message();
    const std::filesystem::path probe = directory / ".yakumo-write-check";
    {
        std::ofstream out(probe, std::ios::trunc | std::ios::binary);
        out << "Yakumo checks that it can write here; this file is removed at once.\n";
        out.close();
        if (!out) {
            std::filesystem::remove(probe, ec);
            return "Yakumo cannot write to its data folder:\n" + path_to_utf8(directory) +
                "\n\nIt keeps its settings and saves there. Move Yakumo to a folder you can write to, or give "
                "your user write access to this one.";
        }
    }
    std::filesystem::remove(probe, ec);
    return {};
}

bool has_user_data(const std::filesystem::path &directory) {
    std::error_code ec;
    return std::filesystem::is_regular_file(directory / kSettingsFile, ec) ||
        std::filesystem::is_regular_file(directory / kExecutableFile, ec) ||
        std::filesystem::is_directory(directory / "ms0", ec);
}

std::uint64_t copy_user_data(
    const std::filesystem::path &from, const std::filesystem::path &to, const CopyProgressFn &progress) {
    namespace fs = std::filesystem;
    struct Item {
        fs::path source;
        fs::path target;
        std::uint64_t size;
    };
    std::vector<Item> items;
    std::vector<fs::path> directories;
    std::uint64_t total = 0;
    for (auto it = fs::recursive_directory_iterator(from); it != fs::recursive_directory_iterator(); ++it) {
        const fs::path relative = fs::relative(it->path(), from);
        const fs::path target = to / relative;
        if (it->is_directory()) {
            directories.push_back(target);
        } else if (it->is_regular_file()) {
            // A copy already there is kept: nothing in the portable folder is
            // ever replaced.
            if (fs::exists(target)) continue;
            const std::uint64_t size = it->file_size();
            items.push_back({it->path(), target, size});
            total += size;
        }
    }
    fs::create_directories(to);
    for (const fs::path &directory : directories) fs::create_directories(directory);
    std::uint64_t done = 0;
    std::vector<char> buffer(1u << 20);
    // On failure or cancellation the files this call copied go again, so a
    // stopped copy leaves the target as it was, apart from empty folders.
    std::vector<fs::path> copied;
    const auto undo = [&copied] {
        std::error_code ignored;
        for (const fs::path &file : copied) fs::remove(file, ignored);
    };
    for (const Item &item : items) {
        fs::create_directories(item.target.parent_path());
        fs::path partial = item.target;
        partial += ".part";
        try {
            std::ifstream in(item.source, std::ios::binary);
            std::ofstream out(partial, std::ios::binary | std::ios::trunc);
            if (!in || !out)
                throw fs::filesystem_error(
                    "cannot copy", item.source, item.target, std::make_error_code(std::errc::io_error));
            while (in) {
                in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const std::streamsize got = in.gcount();
                if (got <= 0) break;
                out.write(buffer.data(), got);
                if (!out)
                    throw fs::filesystem_error("cannot write", partial, std::make_error_code(std::errc::io_error));
                done += static_cast<std::uint64_t>(got);
                if (progress) progress(done, total);
            }
            out.close();
            if (!out) throw fs::filesystem_error("cannot write", partial, std::make_error_code(std::errc::io_error));
            fs::rename(partial, item.target);
            copied.push_back(item.target);
        } catch (...) {
            std::error_code ignored;
            fs::remove(partial, ignored);
            undo();
            throw;
        }
    }
    if (progress) progress(total, total);
    return items.size();
}

SettingsEntries read_settings_file(const std::filesystem::path &data_dir) {
    SettingsEntries entries;
    std::ifstream in(data_dir / kSettingsFile);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = trim(line.substr(0, equals));
        if (!key.empty()) entries[key] = trim(line.substr(equals + 1u));
    }
    return entries;
}

void write_settings_file(const std::filesystem::path &data_dir, const SettingsEntries &entries) {
    std::filesystem::create_directories(data_dir);
    const std::filesystem::path target = data_dir / kSettingsFile;
    const std::filesystem::path partial = data_dir / (std::string(kSettingsFile) + ".part");
    {
        std::ofstream out(partial, std::ios::trunc);
        out << "# Written by Yakumo: the installer and the in-game menu (Esc, or L3+R3 on a gamepad).\n"
            << "# disc_image: the disc image to play from; a relative path is inside this directory.\n";
        for (const auto &[key, value] : entries) out << key << "=" << value << "\n";
        if (!out) throw psprecomp::Error("Cannot write " + path_to_utf8(partial));
    }
    std::filesystem::rename(partial, target);
}

UserSettings load_settings(const std::filesystem::path &data_dir) {
    UserSettings settings;
    const SettingsEntries entries = read_settings_file(data_dir);
    if (const auto found = entries.find("disc_image"); found != entries.end() && !found->second.empty())
        settings.disc_image = path_from_utf8(found->second);
    return settings;
}

void save_settings(const std::filesystem::path &data_dir, const UserSettings &settings) {
    SettingsEntries entries = read_settings_file(data_dir);
    entries["disc_image"] = path_to_utf8(settings.disc_image);
    write_settings_file(data_dir, entries);
}

std::optional<Installation> find_installation(const std::filesystem::path &data_dir) {
    std::error_code ec;
    const std::filesystem::path executable = data_dir / kExecutableFile;
    if (!std::filesystem::is_regular_file(executable, ec)) return std::nullopt;
    const UserSettings settings = load_settings(data_dir);
    Installation installation;
    installation.executable = executable;
    installation.disc_image = settings.disc_image.is_absolute() ? settings.disc_image : data_dir / settings.disc_image;
    installation.image_copied = !settings.disc_image.is_absolute();
    return installation;
}

} // namespace mhp3rd::install
