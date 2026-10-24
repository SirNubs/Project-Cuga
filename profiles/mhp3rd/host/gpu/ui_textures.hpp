#pragma once

#include "ge_state.hpp"

#include "texture_decode.hpp"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

// Sharper game UI at a high internal resolution (issue #164): the CPU side of
// what the renderer draws instead of the game's own 2D textures. Nothing here
// changes what the game reads; only the pictures the renderer samples.
namespace mhp3rd::gpu::ui {

// The game's glyph atlas (traced with MHP3RD_TRACE_FONT and
// MHP3RD_TRACE_SPRITES in the village, the Guild Hall and a quest): the
// object sceLibFont's caller keeps its glyphs in (fonts::game_atlas) holds, at
// +22168, a u16 atlas cell per character code (0xFFFF: none), and from
// +0x29700 eight 256x256 CLUT4 pages, swizzled, right after the 20x20 buffer each glyph
// is drawn into (+0x29630, 200 bytes). A page holds 12 x 11 cells, 20 texels
// wide and 22 apart vertically, numbered along rows; cell n is on page n / 132.
inline constexpr std::uint32_t kCodeToCell = 22168u;
inline constexpr std::uint32_t kCodeToCellEntries = 0xFFF0u;
inline constexpr std::uint32_t kAtlasPages = 0x29700u;
inline constexpr std::uint32_t kAtlasPageBytes = 0x8000u;
inline constexpr std::uint32_t kAtlasPageCount = 8u;
inline constexpr std::uint32_t kCellsPerRow = 12u;
inline constexpr std::uint32_t kCellsPerPage = 132u;
inline constexpr std::uint32_t kCellWidth = 20u;
inline constexpr std::uint32_t kCellPitch = 22u;

// Whether `texture` is one of the atlas's pages as the game draws them.
[[nodiscard]] bool is_glyph_page(const TextureState &texture);

// A page of the atlas drawn `scale` times as large, RGBA8 through the page's
// own palette: each cell whose character is known is its glyph drawn again
// at that size (fonts::glyph_cell); every other texel is the page's own,
// repeated. False when the page does not look like the atlas: more than a
// few of its known cells differ from their glyphs drawn again at scale 1.
struct GlyphPageReport {
    std::uint32_t cells{};   // cells with a known character
    std::uint32_t redrawn{}; // of those, drawn again
    std::uint32_t mismatched{};
};
bool glyph_page(const GuestMemory &memory, const TextureState &texture, int scale, std::vector<std::uint32_t> &out,
    GlyphPageReport &report);

// MMPX copies are made on a thread of their own, so a texture seen for the
// first time (a menu opening) never holds up a frame: the original is drawn
// until its copy is ready. submit() takes a copy of what decoding reads;
// take() hands over a finished image once.
class Upscaler {
public:
    Upscaler() = default;
    Upscaler(const Upscaler &) = delete;
    Upscaler &operator=(const Upscaler &) = delete;
    ~Upscaler();
    // False when the texture cannot be copied for another thread.
    bool submit(std::uint64_t key, const GuestMemory &memory, const TextureState &texture, std::uint32_t doublings);
    [[nodiscard]] bool pending(std::uint64_t key) const;
    // True once, with the image, when the copy for `key` is done; an empty
    // image when it could not be made.
    bool take(std::uint64_t key, std::vector<std::uint32_t> &pixels, std::uint32_t &width, std::uint32_t &height,
        double &milliseconds);
    [[nodiscard]] std::size_t queued() const;

private:
    struct Job {
        std::uint64_t key{};
        TextureSnapshot snapshot;
        std::uint32_t doublings{};
        std::vector<std::uint32_t> pixels;
        std::uint32_t width{};
        std::uint32_t height{};
        double milliseconds{};
        bool done{};
    };
    void run();
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::shared_ptr<Job>> queue_;
    std::unordered_map<std::uint64_t, std::shared_ptr<Job>> jobs_;
    std::thread thread_;
    bool stop_{};
};

} // namespace mhp3rd::gpu::ui
