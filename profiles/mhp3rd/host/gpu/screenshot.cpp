#include "gpu/screenshot.hpp"

#include "install/user_data.hpp"

#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace mhp3rd::screenshot {
namespace {

std::tm local_time(std::chrono::system_clock::time_point when) {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(when);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &seconds);
#else
    localtime_r(&seconds, &tm);
#endif
    return tm;
}

// The write in progress. Joined before the next one starts and when the
// program ends, so a screenshot taken just before quitting is still written.
struct Writer {
    std::mutex mutex;
    std::thread thread;
    // Every path handed to a write: one still being encoded is not on disk
    // yet, and a second screenshot in the same second must not take its name.
    std::set<std::filesystem::path> taken;
    ~Writer() {
        if (thread.joinable()) thread.join();
    }
};

Writer &writer() {
    static Writer value;
    return value;
}

} // namespace

std::filesystem::path folder() {
    return install::user_data_directory() / "screenshots";
}

std::string file_name(std::chrono::system_clock::time_point when) {
    const std::tm tm = local_time(when);
    char text[64];
    std::strftime(text, sizeof(text), "Yakumo_%Y-%m-%d_%H-%M-%S", &tm);
    return std::string(text) + ".png";
}

std::filesystem::path free_path(const std::filesystem::path &directory, std::chrono::system_clock::time_point when) {
    const std::string name = file_name(when);
    const std::string stem = name.substr(0, name.size() - 4u);
    std::filesystem::path path = directory / name;
    std::error_code ec;
    Writer &w = writer();
    const std::lock_guard lock(w.mutex);
    for (int n = 2; std::filesystem::exists(path, ec) || w.taken.count(path) != 0u; ++n)
        path = directory / (stem + "_" + std::to_string(n) + ".png");
    return path;
}

bool write_png(const std::filesystem::path &path, const std::vector<std::uint8_t> &rgba, std::uint32_t width,
    std::uint32_t height, std::string &error) {
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    if (width == 0u || height == 0u || rgba.size() < pixels * 4u) {
        error = "no picture";
        return false;
    }
    std::vector<std::uint8_t> rgb(pixels * 3u);
    for (std::size_t i = 0; i < pixels; ++i) {
        rgb[i * 3u + 0u] = rgba[i * 4u + 0u];
        rgb[i * 3u + 1u] = rgba[i * 4u + 1u];
        rgb[i * 3u + 2u] = rgba[i * 4u + 2u];
    }
    int length = 0;
    unsigned char *png = stbi_write_png_to_mem(
        rgb.data(), static_cast<int>(width * 3u), static_cast<int>(width), static_cast<int>(height), 3, &length);
    if (png == nullptr) {
        error = "the picture could not be encoded";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    if (out) out.write(reinterpret_cast<const char *>(png), length);
    STBIW_FREE(png);
    out.close();
    if (!out) {
        error = "cannot write " + install::path_to_utf8(path);
        return false;
    }
    return true;
}

void save_png_later(
    std::filesystem::path path, std::vector<std::uint8_t> rgba, std::uint32_t width, std::uint32_t height) {
    Writer &w = writer();
    const std::lock_guard lock(w.mutex);
    if (w.thread.joinable()) w.thread.join();
    w.taken.insert(path);
    w.thread = std::thread([path = std::move(path), rgba = std::move(rgba), width, height] {
        std::string error;
        if (write_png(path, rgba, width, height, error))
            std::cout << "[screenshot] " << install::path_to_utf8(path) << " (" << width << "x" << height << ")"
                      << std::endl;
        else
            std::cout << "[screenshot] not saved: " << error << std::endl;
    });
}

void finish_writes() {
    Writer &w = writer();
    const std::lock_guard lock(w.mutex);
    if (w.thread.joinable()) w.thread.join();
}

} // namespace mhp3rd::screenshot
