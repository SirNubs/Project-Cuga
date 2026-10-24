#include "platform/utf8_path.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace mhp2g {

std::string path_to_utf8(const std::filesystem::path &path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}

std::filesystem::path path_from_utf8(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

#if defined(_WIN32)

std::wstring widen(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), length);
    return wide;
}

std::string narrow(std::wstring_view wide) {
    if (wide.empty()) return {};
    const int length =
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string utf8(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), length, nullptr, nullptr);
    return utf8;
}

std::optional<std::string> environment_utf8(const char *name) {
    const std::wstring wide_name = widen(name);
    SetLastError(ERROR_SUCCESS);
    const DWORD needed = GetEnvironmentVariableW(wide_name.c_str(), nullptr, 0); // with the terminator
    if (needed == 0u) return GetLastError() == ERROR_ENVVAR_NOT_FOUND ? std::nullopt : std::optional<std::string>("");
    std::wstring value(needed, L'\0');
    const DWORD written = GetEnvironmentVariableW(wide_name.c_str(), value.data(), needed);
    if (written >= needed) return std::nullopt; // changed in between
    value.resize(written);
    return narrow(value);
}

std::vector<std::string> utf8_arguments(int argc, char **argv) {
    int count = 0;
    wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::string> arguments;
    if (wide != nullptr) {
        for (int i = 0; i < count; ++i) arguments.push_back(narrow(wide[i]));
        LocalFree(wide);
        return arguments;
    }
    for (int i = 0; i < argc; ++i) arguments.emplace_back(argv[i]);
    return arguments;
}

void use_utf8_console() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

std::string folder_url(const std::filesystem::path &path) {
    std::filesystem::path native = path;
    native.make_preferred();
    return path_to_utf8(native);
}

#else

std::optional<std::string> environment_utf8(const char *name) {
    const char *value = std::getenv(name);
    if (value == nullptr) return std::nullopt;
    return std::string(value);
}

std::vector<std::string> utf8_arguments(int argc, char **argv) {
    return std::vector<std::string>(argv, argv + argc);
}

void use_utf8_console() {}

std::string folder_url(const std::filesystem::path &path) {
    std::string url = "file://";
    for (const unsigned char c : path_to_utf8(path)) {
        if (std::isalnum(c) != 0 || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') {
            url += static_cast<char>(c);
        } else {
            char escaped[4];
            std::snprintf(escaped, sizeof(escaped), "%%%02X", c);
            url += escaped;
        }
    }
    return url;
}

#endif

std::filesystem::path environment_path(const char *name) {
    const std::optional<std::string> value = environment_utf8(name);
    return value && !value->empty() ? path_from_utf8(*value) : std::filesystem::path{};
}

} // namespace mhp2g
