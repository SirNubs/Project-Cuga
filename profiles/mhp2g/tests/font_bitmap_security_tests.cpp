#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

static int allocation_calls;
static int live_allocations;
static int fail_allocation;
static std::size_t largest_request;
static void *test_allocate(std::size_t size, void *) {
    ++allocation_calls;
    largest_request = std::max(largest_request, size);
    if (allocation_calls == fail_allocation || size > 1024 * 1024) return nullptr;
    void *result = std::malloc(size);
    if (result) ++live_allocations;
    return result;
}
static void test_free(void *pointer, void *) {
    if (pointer) --live_allocations;
    std::free(pointer);
}
static void *test_clear(void *pointer, int value, std::size_t size) {
    if (size > 1024 * 1024) throw std::runtime_error("unbounded clear request");
    return std::memset(pointer, value, size);
}
#define STBTT_malloc test_allocate
#define STBTT_free test_free
#define STBTT_memcpy memcpy
#define STBTT_memset test_clear
#define STB_TRUETYPE_IMPLEMENTATION
#ifdef TEST_IMGUI_STB
#include "imstb_truetype.h"
#else
#include "stb_truetype.h"
#endif

static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
static void reset_allocator(int fail = 0) {
    require(live_allocations == 0, "allocation leak");
    allocation_calls = 0;
    largest_request = 0;
    fail_allocation = fail;
}

// An independently authored minimal sfnt with one square glyph, no external font.
static std::array<unsigned char, 640> square_font() {
    std::array<unsigned char, 640> data{};
    auto u16 = [&](int at, unsigned value) {
        data[at] = static_cast<unsigned char>(value >> 8);
        data[at + 1] = static_cast<unsigned char>(value);
    };
    auto u32 = [&](int at, unsigned value) {
        u16(at, value >> 16);
        u16(at + 2, value);
    };
    u32(0, 0x00010000);
    u16(4, 7);
    const char *tags[] = {"cmap", "head", "hhea", "hmtx", "maxp", "loca", "glyf"};
    const unsigned offsets[] = {128, 416, 480, 528, 544, 560, 576};
    const unsigned lengths[] = {274, 54, 36, 4, 6, 8, 34};
    for (int i = 0; i < 7; ++i) {
        std::memcpy(data.data() + 12 + i * 16, tags[i], 4);
        u32(20 + i * 16, offsets[i]);
        u32(24 + i * 16, lengths[i]);
    }
    u16(130, 1);
    u16(132, 3);
    u16(134, 1);
    u32(136, 12);
    u16(142, 262); // format 0 cmap: every codepoint maps to glyph 0.
    u16(434, 20);
    u16(466, 1); // units per em, long loca offsets.
    u16(484, 20);
    u16(514, 1);
    u16(528, 20); // ascent and horizontal metrics.
    u16(548, 1);
    u32(564, 34);
    u16(576, 1);
    u16(582, 20);
    u16(584, 20);                                  // contour and bounding box.
    u16(586, 3);                                   // last point of the four-point contour; no instructions.
    for (int i = 0; i < 4; ++i) data[590 + i] = 1; // on-curve, signed deltas.
    u16(596, 20);
    u16(600, static_cast<unsigned>(-20));
    u16(606, 20);
    return data;
}

int main(int argc, char **argv) {
    try {
        auto data = square_font();
        stbtt_fontinfo font{};
        require(stbtt_InitFont(&font, data.data(), 0) != 0, "synthetic font initialization");
        const std::string mode = argc > 1 ? argv[1] : "all";
        if (mode == "benchmark") {
            reset_allocator();
            auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < 10000; ++i) {
                auto *bitmap = stbtt_GetGlyphBitmap(&font, 1, 1, 0, nullptr, nullptr, nullptr, nullptr);
                require(bitmap != nullptr, "benchmark glyph allocation");
                stbtt_FreeBitmap(bitmap, nullptr);
            }
            require(live_allocations == 0, "benchmark leaked");
            std::printf("10000 small glyphs: %.3f ms\n",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            return 0;
        }
        if (mode == "all" || mode == "scanline-overflow") {
            reset_allocator();
            unsigned char pixel = 0x7b;
            stbtt__bitmap bitmap{INT_MAX, 1, INT_MAX, &pixel};
            stbtt__edge sentinel{};
            stbtt__rasterize_sorted_edges(&bitmap, &sentinel, 0, 1, 0, 0, nullptr);
            require(pixel == 0x7b && live_allocations == 0, "failed wide scanline touched pixels or leaked");
            reset_allocator(1);
            bitmap.w = bitmap.stride = 65;
            stbtt__rasterize_sorted_edges(&bitmap, &sentinel, 0, 1, 0, 0, nullptr);
            require(pixel == 0x7b && live_allocations == 0, "failed scanline allocation touched pixels or leaked");
        }
        if (mode == "all" || mode == "bake-wide") {
            std::array<unsigned char, 2 * 24> pixels{};
            stbtt_bakedchar character{};
            character.x0 = 999;
            require(stbtt_BakeFontBitmap(data.data(), 0, 20, pixels.data(), 2, 24, 65, 1, &character) == 0,
                "wide glyph accepted");
            require(character.x0 == 999, "wide glyph wrote character output");
        }
        if (mode == "all" || mode == "bitmap-overflow") {
            reset_allocator();
            int w = 0, h = 0;
            auto *bitmap = stbtt_GetGlyphBitmap(&font, 5000, 5000, 0, &w, &h, nullptr, nullptr);
            require(bitmap == nullptr && largest_request <= 1024 * 1024, "bitmap size overflow reached allocator");
            require(live_allocations == 0, "bitmap failure leaked shape");
        }
        if (mode == "all" || mode == "pack-overflow") {
            reset_allocator();
            unsigned char pixel = 0x7b;
            stbtt_pack_context pack{};
            require(stbtt_PackBegin(&pack, &pixel, 50000, 50000, 0, 1, nullptr) == 0, "pack overflow accepted");
            require(allocation_calls == 0 && pixel == 0x7b, "invalid pack touched allocator/pixels");
        }
        if (mode == "all" || mode == "bake-overflow") {
            unsigned char pixel = 0x7b;
            require(stbtt_BakeFontBitmap(data.data(), 0, 20, &pixel, 50000, 50000, 65, 0, nullptr) == -1,
                "bake overflow accepted");
            require(pixel == 0x7b, "invalid bake touched pixels");
        }
        if (mode == "all" || mode == "sdf-size-overflow") {
            reset_allocator();
            require(stbtt_GetGlyphSDF(&font, 5000, 0, 2, 128, 16, nullptr, nullptr, nullptr, nullptr) == nullptr,
                "SDF area overflow accepted");
            require(allocation_calls == 0, "invalid SDF area reached allocator");
        }
        if (mode == "all" || mode == "sdf-overflow") {
            reset_allocator();
            require(stbtt_GetGlyphSDF(&font, 1, 0, INT_MAX, 128, 16, nullptr, nullptr, nullptr, nullptr) == nullptr,
                "SDF padding overflow accepted");
            require(allocation_calls == 0, "invalid SDF reached allocator");
        }
        if (mode != "all") return 0;
        for (auto dims : {std::array<int, 5>{-1, 8, 0, 1, 0}, {0, 8, 0, 1, 0}, {8, -1, 0, 1, 0}, {8, 0, 0, 1, 0},
                 {8, 8, 7, 1, 0}, {8, 8, 0, -1, 0}, {8, 8, 0, 8, 0}, {INT_MAX, 2, 0, 0, 0}, {8, 8, INT_MAX, 0, 0}}) {
            reset_allocator();
            stbtt_pack_context pack{};
            require(stbtt_PackBegin(&pack, nullptr, dims[0], dims[1], dims[2], dims[3], nullptr) == 0,
                "invalid pack dimensions accepted");
            require(allocation_calls == 0, "invalid dimensions allocated");
        }
        for (auto dims : {std::array<int, 2>{-1, 8}, {0, 8}, {8, -1}, {8, 0}})
            require(stbtt_BakeFontBitmap(nullptr, 0, 20, nullptr, dims[0], dims[1], 0, 0, nullptr) == -1,
                "invalid bake read font");
        for (float scale :
            {-1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), 1e30f}) {
            reset_allocator();
            auto *bitmap = stbtt_GetGlyphBitmap(&font, scale, scale, 0, nullptr, nullptr, nullptr, nullptr);
            require(bitmap == nullptr && largest_request <= 1024 * 1024, "invalid bitmap scale accepted");
            require(stbtt_GetGlyphSDF(&font, scale, 0, 2, 128, 16, nullptr, nullptr, nullptr, nullptr) == nullptr,
                "invalid SDF scale accepted");
        }
        reset_allocator();
        std::array<unsigned char, 24> pixels;
        pixels.fill(0xcc);
        stbtt_pack_context pack{};
        require(stbtt_PackBegin(&pack, pixels.data(), 5, 3, 8, 1, nullptr) == 1, "padded pack failed");
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 8; ++x)
                require(pixels[y * 8 + x] == (x < 5 ? 0 : 0xcc), "row stride clear overwrote padding or missed pixels");
        stbtt_PackEnd(&pack);
        for (int fail : {1, 2}) {
            reset_allocator(fail);
            require(stbtt_PackBegin(&pack, nullptr, 8, 8, 0, 1, nullptr) == 0 && live_allocations == 0,
                "pack allocation failure leaked");
        }
        for (int fail : {1, 2, 3}) {
            reset_allocator(fail);
            auto *sdf = stbtt_GetGlyphSDF(&font, 1, 0, 2, 128, 16, nullptr, nullptr, nullptr, nullptr);
            require(sdf == nullptr && live_allocations == 0, "SDF allocation failure leaked or returned data");
        }
        reset_allocator();
        int w = 0, h = 0;
        auto *bitmap = stbtt_GetGlyphBitmap(&font, 1, 1, 0, &w, &h, nullptr, nullptr);
        require(bitmap && w == 20 && h == 20, "valid glyph bitmap dimensions");
        require(std::all_of(bitmap, bitmap + w * h, [](unsigned char value) { return value == 255; }),
            "square coverage changed");
        stbtt_FreeBitmap(bitmap, nullptr);
        auto *sdf = stbtt_GetGlyphSDF(&font, 1, 0, 2, 128, 16, &w, &h, nullptr, nullptr);
        require(sdf && w == 24 && h == 24 && sdf[12 * w + 12] > 128 && sdf[0] < 128, "valid SDF changed");
        stbtt_FreeSDF(sdf, nullptr);
        reset_allocator();
        std::array<unsigned char, 32 * 32> baked{};
        stbtt_bakedchar character{};
        require(
            stbtt_BakeFontBitmap(data.data(), 0, 20, baked.data(), 32, 32, 65, 1, &character) > 0, "valid bake failed");
        require(character.x1 - character.x0 == 20 && character.y1 - character.y0 == 20, "baked glyph bounds changed");
        reset_allocator();
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 200; ++i) {
            bitmap = stbtt_GetGlyphBitmap(&font, 1, 1, 0, nullptr, nullptr, nullptr, nullptr);
            require(bitmap != nullptr, "benchmark glyph allocation");
            stbtt_FreeBitmap(bitmap, nullptr);
        }
        require(live_allocations == 0, "valid rasterization leaked");
        std::printf("font bitmap guards passed; 200 small glyphs: %.3f ms\n",
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
