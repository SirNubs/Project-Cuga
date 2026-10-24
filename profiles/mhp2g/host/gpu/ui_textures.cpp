#include "gpu/ui_textures.hpp"

#include "fonts/game_font.hpp"
#include "gpu/mmpx.hpp"
#include "gpu/texture_decode.hpp"

#include <algorithm>
#include <array>
#include <chrono>

namespace mhp2g::gpu::ui {
namespace {

// A page's texels in rows, one ink value (the palette index) per texel: the
// game keeps its pages swizzled, which decoding a copy undoes.
bool page_indices(const GuestMemory &memory, const TextureState &texture, std::vector<std::uint8_t> &indices) {
    TextureSnapshot snapshot;
    if (!snapshot_texture(memory, texture, snapshot)) return false;
    std::vector<std::uint32_t> colours;
    if (!decode_snapshot(snapshot, colours)) return false;
    const std::uint32_t width = texture.width, height = texture.height;
    if (snapshot.texels.size() < static_cast<std::size_t>(snapshot.row_bytes) * height) return false;
    indices.resize(static_cast<std::size_t>(width) * height);
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint8_t byte = snapshot.texels[static_cast<std::size_t>(y) * snapshot.row_bytes + x / 2u];
            indices[static_cast<std::size_t>(y) * width + x] =
                (x & 1u) != 0u ? static_cast<std::uint8_t>(byte >> 4u) : static_cast<std::uint8_t>(byte & 0x0Fu);
        }
    return true;
}

// The page's sixteen colours, through the decoder itself: a 16x1 CLUT4
// picture of the indices 0..15 with the page's palette.
bool palette_of(const GuestMemory &memory, const TextureState &texture, std::array<std::uint32_t, 16> &palette) {
    TextureSnapshot snapshot;
    if (!snapshot_texture(memory, texture, snapshot)) return false;
    snapshot.texture.width = 16u;
    snapshot.texture.height = 1u;
    snapshot.texture.buffer_width = 16u;
    snapshot.texture.swizzled = false;
    snapshot.row_bytes = 8u;
    snapshot.texels = {0x10u, 0x32u, 0x54u, 0x76u, 0x98u, 0xBAu, 0xDCu, 0xFEu};
    std::vector<std::uint32_t> colours;
    if (!decode_snapshot(snapshot, colours) || colours.size() < 16u) return false;
    std::copy_n(colours.begin(), 16u, palette.begin());
    return true;
}

} // namespace

bool is_glyph_page(const TextureState &texture) {
    const std::uint32_t atlas = fonts::game_atlas();
    if (atlas == 0u || texture.format != TextureFormat::Clut4 || texture.width != 256u || texture.height != 256u ||
        texture.buffer_width != 256u)
        return false;
    const std::uint32_t first = atlas + kAtlasPages;
    if (texture.address < first) return false;
    const std::uint32_t offset = texture.address - first;
    return offset % kAtlasPageBytes == 0u && offset / kAtlasPageBytes < kAtlasPageCount;
}

bool glyph_page(const GuestMemory &memory, const TextureState &texture, int scale, std::vector<std::uint32_t> &out,
    GlyphPageReport &report) {
    report = {};
    if (!is_glyph_page(texture) || scale < 2) return false;
    const std::uint32_t atlas = fonts::game_atlas();
    const std::uint32_t page = (texture.address - atlas - kAtlasPages) / kAtlasPageBytes;
    std::array<std::uint32_t, 16> palette{};
    std::vector<std::uint8_t> indices;
    if (!memory.contains(atlas + kCodeToCell, kCodeToCellEntries * 2u) || !palette_of(memory, texture, palette) ||
        !page_indices(memory, texture, indices))
        return false;
    const auto index_at = [&](std::uint32_t x, std::uint32_t y) { return indices[y * 256u + x]; };

    // Which character is in each cell of this page.
    std::array<std::uint32_t, kCellsPerPage> codes{};
    codes.fill(~0u);
    for (std::uint32_t code = 0; code < kCodeToCellEntries; ++code) {
        const std::uint32_t cell = memory.load16(atlas + kCodeToCell + code * 2u);
        if (cell != 0xFFFFu && cell / kCellsPerPage == page) codes[cell % kCellsPerPage] = code;
    }

    const auto size = static_cast<std::uint32_t>(scale);
    const std::uint32_t side = 256u * size;
    out.assign(static_cast<std::size_t>(side) * side, 0u);
    // Everything as it is, texel for texel, then the known cells drawn again.
    for (std::uint32_t y = 0; y < 256u; ++y)
        for (std::uint32_t x = 0; x < 256u; ++x) {
            const std::uint32_t colour = palette[index_at(x, y)];
            for (std::uint32_t dy = 0; dy < size; ++dy)
                std::fill_n(out.begin() +
                        static_cast<std::ptrdiff_t>(
                            (static_cast<std::size_t>(y) * size + dy) * side + static_cast<std::size_t>(x) * size),
                    size, colour);
        }
    for (std::uint32_t cell = 0; cell < kCellsPerPage; ++cell) {
        if (codes[cell] == ~0u) continue;
        ++report.cells;
        const std::uint32_t left = cell % kCellsPerRow * kCellWidth;
        const std::uint32_t top = cell / kCellsPerRow * kCellPitch;
        // The cell must hold what the noted passes give at scale 1; one that
        // does not (drawn before the passes were noted, or by other code) keeps
        // the game's texels.
        const std::vector<std::uint8_t> check = fonts::glyph_cell(codes[cell], 1);
        if (check.empty()) continue;
        bool same = true;
        for (std::uint32_t y = 0; y < kCellWidth && same; ++y)
            for (std::uint32_t x = 0; x < kCellWidth && same; ++x)
                same = check[y * kCellWidth + x] == index_at(left + x, top + y);
        if (!same) {
            ++report.mismatched;
            continue;
        }
        const std::vector<std::uint8_t> ink = fonts::glyph_cell(codes[cell], scale);
        const std::uint32_t cell_side = kCellWidth * size;
        for (std::uint32_t y = 0; y < cell_side; ++y)
            for (std::uint32_t x = 0; x < cell_side; ++x)
                out[(static_cast<std::size_t>(top) * size + y) * side + static_cast<std::size_t>(left) * size + x] =
                    palette[ink[static_cast<std::size_t>(y) * cell_side + x]];
        ++report.redrawn;
    }
    // A page that is not what it was traced to be is left alone.
    return report.mismatched * 4u <= report.cells;
}

Upscaler::~Upscaler() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

bool Upscaler::submit(
    std::uint64_t key, const GuestMemory &memory, const TextureState &texture, std::uint32_t doublings) {
    auto job = std::make_shared<Job>();
    job->key = key;
    job->doublings = doublings;
    if (!snapshot_texture(memory, texture, job->snapshot)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (jobs_.count(key) != 0u) return true;
    // Copies made and never asked for again (a texture seen for a moment)
    // are dropped rather than kept.
    if (jobs_.size() >= 32u) std::erase_if(jobs_, [](const auto &entry) { return entry.second->done; });
    jobs_.emplace(key, job);
    queue_.push_back(std::move(job));
    if (!thread_.joinable()) thread_ = std::thread([this] { run(); });
    wake_.notify_one();
    return true;
}

bool Upscaler::pending(std::uint64_t key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_.count(key) != 0u;
}

std::size_t Upscaler::queued() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

bool Upscaler::take(std::uint64_t key, std::vector<std::uint32_t> &pixels, std::uint32_t &width, std::uint32_t &height,
    double &milliseconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = jobs_.find(key);
    if (found == jobs_.end() || !found->second->done) return false;
    pixels = std::move(found->second->pixels);
    width = found->second->width;
    height = found->second->height;
    milliseconds = found->second->milliseconds;
    jobs_.erase(found);
    return true;
}

void Upscaler::run() {
    for (;;) {
        std::shared_ptr<Job> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        const auto start = std::chrono::steady_clock::now();
        std::vector<std::uint32_t> image, doubled;
        std::uint32_t width = job->snapshot.texture.width, height = job->snapshot.texture.height;
        if (decode_snapshot(job->snapshot, image) && !image.empty()) {
            for (std::uint32_t i = 0; i < job->doublings; ++i) {
                mmpx_2x(image.data(), width, height, doubled);
                image.swap(doubled);
                width *= 2u;
                height *= 2u;
            }
        } else {
            image.clear();
        }
        const double milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::lock_guard<std::mutex> lock(mutex_);
        job->pixels = std::move(image);
        job->width = width;
        job->height = height;
        job->milliseconds = milliseconds;
        job->done = true;
    }
}

} // namespace mhp2g::gpu::ui
