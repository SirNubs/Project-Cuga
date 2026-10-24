// Screenshots: file names, and the PNG files written, read back. Works in a
// temporary directory; no game data.
#include "gpu/screenshot.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#define STBI_ONLY_PNG
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace {
namespace fs = std::filesystem;
using namespace mhp3rd::screenshot;

int failures{};

void check(bool condition, const char *message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void test_names(const fs::path &dir) {
    const auto when = std::chrono::system_clock::now();
    const std::string name = file_name(when);
    check(name.size() == std::string("Yakumo_2026-09-27_14-03-22.png").size() && name.starts_with("Yakumo_") &&
            name.ends_with(".png") && name.find(' ') == std::string::npos && name.find(':') == std::string::npos,
        "names carry the date and time, with nothing a file system refuses");
    check(free_path(dir, when) == dir / name, "a free name is used as it is");
    std::ofstream(dir / name) << "x";
    const fs::path second = free_path(dir, when);
    check(second == dir / (name.substr(0, name.size() - 4u) + "_2.png"), "a second one in the same second gets _2");
    std::ofstream(second) << "x";
    check(free_path(dir, when).filename().string().ends_with("_3.png"), "and a third _3");
}

void test_png(const fs::path &dir) {
    constexpr std::uint32_t kWidth = 7u, kHeight = 3u; // odd sizes: no row padding assumed
    std::vector<std::uint8_t> rgba(kWidth * kHeight * 4u);
    for (std::uint32_t i = 0; i < kWidth * kHeight; ++i) {
        rgba[i * 4u + 0u] = static_cast<std::uint8_t>(i * 11u);
        rgba[i * 4u + 1u] = static_cast<std::uint8_t>(255u - i);
        rgba[i * 4u + 2u] = static_cast<std::uint8_t>(i * 3u);
        rgba[i * 4u + 3u] = static_cast<std::uint8_t>(i); // the PSP's stencil, not transparency
    }
    const fs::path path = dir / "sub" / "shot.png";
    std::string error;
    check(write_png(path, rgba, kWidth, kHeight, error), "a picture is written, its folder made");
    int width = 0, height = 0, channels = 0;
    unsigned char *pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 0);
    check(
        pixels != nullptr && width == static_cast<int>(kWidth) && height == static_cast<int>(kHeight) && channels == 3,
        "and reads back at its size, as RGB");
    bool same = pixels != nullptr;
    for (std::uint32_t i = 0; same && i < kWidth * kHeight; ++i)
        for (std::uint32_t c = 0; c < 3u; ++c) same = same && pixels[i * 3u + c] == rgba[i * 4u + c];
    check(same, "with every pixel as it was, top row first");
    stbi_image_free(pixels);
    check(!write_png(dir / "none.png", {}, 0u, 0u, error) && !error.empty(), "no picture, no file");
    save_png_later(dir / "later.png", rgba, kWidth, kHeight);
    finish_writes();
    check(fs::exists(dir / "later.png"), "a write on the side is finished when asked");
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() /
        ("yakumo-screenshot-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    test_names(dir);
    test_png(dir);
    std::error_code ec;
    fs::remove_all(dir, ec);
    if (failures != 0) {
        std::cerr << failures << " screenshot test(s) failed\n";
        return 1;
    }
    std::cout << "screenshot tests passed\n";
    return 0;
}
