#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Screenshots (#187): the game's picture as the renderer drew it, at the full
// size it was drawn at, saved as PNG files in screenshots/ in the data
// directory. The picture is the game's own render target, so nothing the
// port draws over the window (the menu, notes, the free camera's line, the
// performance overlay) is ever in it. Nothing here needs SDL or Vulkan.
namespace mhp3rd::screenshot {

// screenshots/ in the data directory, portable or per user.
[[nodiscard]] std::filesystem::path folder();

// "Yakumo_2026-09-27_14-03-22.png", in local time.
[[nodiscard]] std::string file_name(std::chrono::system_clock::time_point when);

// The file in `folder` for a screenshot taken `when`: file_name, or with
// "_2", "_3", ... before ".png" when a file of that name exists already, as
// it does for two screenshots in one second, or when save_png_later was
// given that path already.
[[nodiscard]] std::filesystem::path free_path(
    const std::filesystem::path &folder, std::chrono::system_clock::time_point when);

// Encodes `rgba` (4 bytes a pixel, top row first; alpha is left out, as the
// PSP's alpha is the stencil, not transparency) as an RGB PNG and writes it
// to `path`, creating its folder. False with `error` filled on failure.
bool write_png(const std::filesystem::path &path, const std::vector<std::uint8_t> &rgba, std::uint32_t width,
    std::uint32_t height, std::string &error);

// write_png on a background thread, so encoding a large picture does not
// hold the game up; the result goes to the log. At most one write runs at a
// time: a second waits for the first.
void save_png_later(
    std::filesystem::path path, std::vector<std::uint8_t> rgba, std::uint32_t width, std::uint32_t height);
// Waits for the write in progress, if any.
void finish_writes();

} // namespace mhp3rd::screenshot
