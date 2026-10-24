#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Paths and the text that names them, the same way on every platform.
//
// Text in the host is UTF-8: settings.ini, Dear ImGui, SDL, the logs. Paths
// stay std::filesystem::path from where they are found to where a file is
// opened, and cross into text only through these functions. The narrow forms
// the standard library offers are not UTF-8 on Windows: path::string() and
// path(std::string) use the ANSI code page there, which cannot hold most
// names (a Cyrillic user name on a Western system, Japanese on a Russian one)
// and throws or loses characters; so do main's argv and std::getenv.
namespace mhp3rd {

// A path as UTF-8, for messages, settings and SDL.
[[nodiscard]] std::string path_to_utf8(const std::filesystem::path &path);
// A path from UTF-8 text.
[[nodiscard]] std::filesystem::path path_from_utf8(std::string_view text);

// An environment variable's value as UTF-8; nothing when it is not set.
[[nodiscard]] std::optional<std::string> environment_utf8(const char *name);
// An environment variable that names a path; empty when unset or empty.
[[nodiscard]] std::filesystem::path environment_path(const char *name);

// The program's arguments as UTF-8. On Windows they are read again from the
// wide command line, since main's argv is in the ANSI code page.
[[nodiscard]] std::vector<std::string> utf8_arguments(int argc, char **argv);

// Lets the console show the UTF-8 the host prints (Windows); nothing elsewhere.
void use_utf8_console();

// What SDL_OpenURL needs to show a folder in the file manager: a file:// URL,
// or on Windows the path itself, which the shell opens as it is.
[[nodiscard]] std::string folder_url(const std::filesystem::path &path);

#if defined(_WIN32)
[[nodiscard]] std::wstring widen(std::string_view utf8);
[[nodiscard]] std::string narrow(std::wstring_view wide);
#endif

} // namespace mhp3rd
