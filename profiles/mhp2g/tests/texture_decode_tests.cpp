// Synthetic PSP textures with independently specified pixel results.
#include "gpu/texture_decode.hpp"
#include "perf/frame_stats.hpp"

#include <array>
#include <cstdlib>
#include <sstream>
#include <cstdint>
#include <iostream>
#include <vector>

bool mhp3rd::perf::alternate_off(NewPath) {
    return false;
}

namespace {
using namespace mhp3rd::gpu;
int failures{};
void expect(bool condition, const char *message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
constexpr std::uint32_t pixels = 0x08001000u, palette = 0x08010000u;
TextureState state(TextureFormat format, std::uint16_t width = 4, std::uint16_t height = 1) {
    TextureState t{};
    t.address = pixels;
    t.width = width;
    t.height = height;
    t.format = format;
    t.clut_address = palette;
    t.clut_format = 3;
    t.clut_mask = 255;
    return t;
}
void verify(psprecomp::GuestMemory &memory, const TextureState &t, const std::vector<std::uint32_t> &expected) {
    std::vector<std::uint32_t> actual;
    expect(decode_texture(memory, t, actual), "texture decodes");
    expect(actual == expected, "decoded pixels equal specified colors");
    TextureSnapshot snapshot{};
    expect(snapshot_texture(memory, t, snapshot), "direct/indexed texture snapshots");
    // Decoding the owned snapshot must be independent of later guest changes.
    memory.store8(t.address, static_cast<std::uint8_t>(memory.load8(t.address) ^ 0xffu));
    memory.store32(palette, 0x12345678u);
    expect(decode_snapshot(snapshot, actual), "snapshot decodes");
    expect(actual == expected, "snapshot keeps original texels and palette");
}
void direct_formats() {
    psprecomp::GuestMemory memory;
    for (const auto format : {TextureFormat::Rgba5650, TextureFormat::Rgba5551, TextureFormat::Rgba4444}) {
        memory.store16(pixels, 0x001fu);
        memory.store16(pixels + 2, 0xffffu);
        memory.store16(pixels + 4, 0);
        memory.store16(pixels + 6, 0x8000u);
        std::vector<std::uint32_t> expected;
        if (format == TextureFormat::Rgba5650) expected = {0xff0000ffu, 0xffffffffu, 0xff000000u, 0xff830000u};
        if (format == TextureFormat::Rgba5551) expected = {0x000000ffu, 0xffffffffu, 0, 0xff000000u};
        if (format == TextureFormat::Rgba4444) expected = {0x000011ffu, 0xffffffffu, 0, 0x88000000u};
        verify(memory, state(format), expected);
    }
    memory.store32(pixels, 0x01234567u);
    memory.store32(pixels + 4, 0x89abcdefu);
    memory.store32(pixels + 8, 0);
    memory.store32(pixels + 12, 0xffffffffu);
    verify(memory, state(TextureFormat::Rgba8888), {0x01234567u, 0x89abcdefu, 0, 0xffffffffu});
}
void indexed_formats() {
    psprecomp::GuestMemory memory;
    for (auto format : {TextureFormat::Clut4, TextureFormat::Clut8, TextureFormat::Clut16, TextureFormat::Clut32}) {
        for (std::uint32_t i = 0; i < 512; ++i) memory.store32(palette + i * 4, 0x80000000u + i);
        auto t = state(format);
        t.clut_shift = 1;
        t.clut_mask = 3;
        t.clut_offset = 2;
        if (format == TextureFormat::Clut4) {
            memory.store8(pixels, 0x62);
            memory.store8(pixels + 1, 0x2a);
        } else
            for (std::uint32_t i = 0; i < 4; ++i) {
                const std::uint32_t index = std::array<std::uint32_t, 4>{2, 6, 10, 2}[i];
                if (format == TextureFormat::Clut8) memory.store8(pixels + i, static_cast<std::uint8_t>(index));
                if (format == TextureFormat::Clut16) memory.store16(pixels + 2 * i, static_cast<std::uint16_t>(index));
                if (format == TextureFormat::Clut32) memory.store32(pixels + 4 * i, index);
            }
        verify(memory, t, {0x80000021u, 0x80000023u, 0x80000021u, 0x80000021u});
    }
    for (std::uint32_t format = 0; format < 3; ++format) {
        auto t = state(TextureFormat::Clut8, 2);
        t.clut_format = format;
        memory.store8(pixels, 1);
        memory.store8(pixels + 1, 2);
        memory.store16(palette + 2, 0xffff);
        memory.store16(palette + 4, 0);
        verify(memory, t, {0xffffffffu, format == 0 ? 0xff000000u : 0u});
    }
}
void palette_windows() {
    psprecomp::GuestMemory memory;
    for (std::uint32_t format = 0; format < 4; ++format) {
        for (std::uint32_t start : {0u, 1u})
            for (std::uint32_t shift : {0u, 4u}) {
                auto t = state(TextureFormat::Clut8);
                t.clut_format = format;
                t.clut_offset = start;
                t.clut_shift = shift;
                t.clut_mask = 1;
                for (std::size_t i = 0; i < 4; ++i)
                    memory.store8(pixels + i, std::array<std::uint8_t, 4>{0x11, 0x20, 0x30, 0x7f}[i]);
                if (format == 3) {
                    memory.store32(palette, 0);
                    memory.store32(palette + 4, 0xffffffff);
                    memory.store32(palette + 64, 0xff00ff00);
                    memory.store32(palette + 68, 0xffff00ff);
                } else {
                    memory.store16(palette, 0);
                    memory.store16(palette + 2, 0xffff);
                    memory.store16(palette + 32, std::array<std::uint16_t, 3>{0x07e0, 0x83e0, 0xf0f0}[format]);
                    memory.store16(palette + 34, std::array<std::uint16_t, 3>{0xf81f, 0xfc1f, 0xff0f}[format]);
                }
                const auto low = start ? 0xff00ff00u : format == 0 ? 0xff000000u : 0u;
                const auto high = start ? 0xffff00ffu : 0xffffffffu;
                verify(memory, t, {high, low, shift ? high : low, high});
            }
    }
}
void rows_and_swizzle() {
    psprecomp::GuestMemory memory;
    for (std::uint32_t i = 0; i < 12; ++i) memory.store32(pixels + 4 * i, 0xa0000000u + i);
    auto t = state(TextureFormat::Rgba8888, 4, 2);
    t.buffer_width = 2;
    verify(memory, t,
        {0xa0000000u, 0xa0000001u, 0xa0000002u, 0xa0000003u, 0xa0000002u, 0xa0000003u, 0xff000000u, 0xff000000u});
    // Eight rows of two 16-byte blocks, in PSP block-column order.
    t = state(TextureFormat::Rgba8888, 8, 8);
    t.swizzled = true;
    for (std::uint32_t block = 0; block < 2; ++block)
        for (std::uint32_t y = 0; y < 8; ++y)
            for (std::uint32_t x = 0; x < 4; ++x)
                memory.store32(pixels + block * 128 + y * 16 + x * 4, 0xff000000u + y * 8 + block * 4 + x);
    std::vector<std::uint32_t> expected;
    for (std::uint32_t i = 0; i < 64; ++i) expected.push_back(0xff000000u + i);
    verify(memory, t, expected);
    // Non-block-aligned dimensions leave the already linear data unchanged.
    t = state(TextureFormat::Rgba8888, 2, 1);
    t.swizzled = true;
    memory.store32(pixels, 0x11223344);
    memory.store32(pixels + 4, 0x55667788);
    verify(memory, t, {0x11223344, 0x55667788});
}
void compressed() {
    psprecomp::GuestMemory memory;
    for (auto format : {TextureFormat::Dxt1, TextureFormat::Dxt3, TextureFormat::Dxt5}) {
        auto t = state(format, 4, 4);
        for (std::uint32_t y = 0; y < 4; ++y) memory.store8(pixels + y, 0xe4); // selectors 0,1,2,3
        memory.store16(pixels + 4, 0xf800);
        memory.store16(pixels + 6, 0x001f); // red, blue
        memory.store32(pixels + 8, 0xffffffff);
        memory.store32(pixels + 12, 0xffffffff);
        if (format == TextureFormat::Dxt5) {
            memory.store8(pixels + 14, 255);
            memory.store8(pixels + 15, 0);
        }
        std::vector<std::uint32_t> actual;
        expect(decode_texture(memory, t, actual), "DXT block decodes");
        const std::uint32_t alpha = format == TextureFormat::Dxt5 ? 36u : 255u; // code7: 1/7*255
        const std::array<std::uint32_t, 4> row{
            (alpha << 24) | 0xffu, (alpha << 24) | 0xff0000u, (alpha << 24) | 0x5500aau, (alpha << 24) | 0xaa0055u};
        for (std::size_t i = 0; i < actual.size(); ++i)
            expect(actual[i] == row[i % 4], "DXT selectors and interpolated endpoints");
        TextureSnapshot snapshot;
        expect(!snapshot_texture(memory, t, snapshot), "block textures are not snapshot formats");
    }
    auto t = state(TextureFormat::Dxt1, 3, 2);
    memory.store16(pixels + 4, 0);
    memory.store16(pixels + 6, 0xffff);
    std::vector<std::uint32_t> actual;
    expect(decode_texture(memory, t, actual), "partial DXT block clips to sampled size");
    expect(
        actual == std::vector<std::uint32_t>({0xff000000, 0xffffffff, 0xff7f7f7f, 0xff000000, 0xffffffff, 0xff7f7f7f}),
        "DXT1 three-color mode");
    t.width = 4;
    expect(decode_texture(memory, t, actual), "DXT1 transparent selector decodes");
    expect(actual[3] == 0, "DXT1 selector3 transparent");
    t = state(TextureFormat::Dxt5, 4, 4);
    memory.store8(pixels + 14, 0);
    memory.store8(pixels + 15, 255);
    // Codes 0..7 twice. Low endpoint ordering supplies literal zero/255 at6/7.
    std::uint64_t codes{};
    for (std::uint32_t i = 0; i < 16; ++i) codes |= static_cast<std::uint64_t>(i % 8) << (3 * i);
    for (std::uint32_t i = 0; i < 6; ++i) memory.store8(pixels + 8 + i, static_cast<std::uint8_t>(codes >> (8 * i)));
    expect(decode_texture(memory, t, actual), "DXT5 six-entry alpha mode");
    const std::array<std::uint32_t, 8> alphas{0, 255, 51, 102, 153, 204, 0, 255};
    for (std::size_t i = 0; i < actual.size(); ++i)
        expect((actual[i] >> 24) == alphas[i % 8], "DXT5 alpha endpoints and codes");
}
void mirrored_vram_boundary() {
    psprecomp::GuestMemory memory;
    auto t = state(TextureFormat::Rgba8888, 2);
    t.address = 0x041ffffeu;
    memory.store32(t.address, 0xff112233);
    memory.store32(t.address + 4, 0xff445566);
    std::vector<std::uint32_t> actual;
    expect(decode_texture(memory, t, actual) && actual == std::vector<std::uint32_t>{0xff112233, 0xff445566},
        "texture crossing the physical EDRAM end reads the legal mirrored guest bytes");
    TextureSnapshot snapshot;
    expect(!snapshot_texture(memory, t, snapshot),
        "noncontiguous EDRAM texture declines asynchronous snapshot and uses immediate decoding");
    const auto key = texture_key(memory, t);
    memory.store32(t.address, 0xff778899);
    expect(texture_key(memory, t) != key, "noncontiguous texture key observes rewritten first texel");
    expect(decode_texture(memory, t, actual) && actual == std::vector<std::uint32_t>{0xff778899, 0xff445566},
        "mirrored texture update preserves the adjacent texel across the wrap");
}
void invalid_and_keys() {
    psprecomp::GuestMemory memory;
    std::vector<std::uint32_t> out;
    TextureSnapshot snapshot;
    for (auto size : {0u, 1025u}) {
        auto t = state(TextureFormat::Rgba8888, static_cast<std::uint16_t>(size));
        expect(!decode_texture(memory, t, out), "invalid width rejected");
        expect(!snapshot_texture(memory, t, snapshot), "invalid snapshot width rejected");
    }
    auto t = state(TextureFormat::Rgba8888);
    t.address = 0;
    expect(!decode_texture(memory, t, out), "unmapped texture rejected");
    expect(!snapshot_texture(memory, t, snapshot), "unmapped snapshot rejected");
    t = state(static_cast<TextureFormat>(11));
    expect(!decode_texture(memory, t, out), "unknown texture format rejected");
    t = state(TextureFormat::Clut8);
    t.clut_address = 0;
    expect(!snapshot_texture(memory, t, snapshot), "unmapped palette snapshot rejected");
    t = state(TextureFormat::Clut4, 1);
    expect(!decode_texture(memory, t, out), "zero-byte CLUT4 row rejected");
    t = state(TextureFormat::Dxt1);
    t.address = 0;
    expect(!decode_texture(memory, t, out), "unmapped DXT block rejected");
    t = state(TextureFormat::Rgba8888, 64, 64);
    const auto original = texture_key(memory, t);
    expect(texture_key(memory, t) == original, "cache key deterministic");
    memory.store8(pixels + 257, 42);
    expect(texture_key(memory, t) != original, "small texture key notices unsampled glyph byte");
    const auto changed = texture_key(memory, t);
    t.clut_address = palette;
    memory.store32(palette, 42);
    expect(texture_key(memory, t) == changed, "direct texture key ignores stale palette");
    t.format = TextureFormat::Clut8;
    const auto indexed = texture_key(memory, t);
    memory.store32(palette, 43);
    expect(texture_key(memory, t) != indexed, "indexed key notices palette change");
    t = state(TextureFormat::Rgba8888, 256, 256);
    const auto large = texture_key(memory, t);
    memory.store32(pixels + 256, 72);
    expect(texture_key(memory, t) != large, "large texture key notices sampled pixel");
    t.address = 0;
    expect(texture_key(memory, t) == texture_key(memory, t), "unmapped key safely deterministic");
    for (auto format : {TextureFormat::Dxt1, TextureFormat::Dxt3, TextureFormat::Dxt5}) {
        t = state(format, 4, 4);
        for (unsigned byte = 0; byte < 16; ++byte) memory.store8(pixels + byte, 0);
        const auto before = texture_key(memory, t);
        const unsigned bytes = format == TextureFormat::Dxt1 ? 8 : 16;
        memory.store8(pixels + bytes - 1, 0x7f);
        expect(texture_key(memory, t) != before,
            "compressed cache key observes the final byte of each independently sized DXT block");
        const auto inside = texture_key(memory, t);
        memory.store8(pixels + bytes + 3, 0x55);
        expect(
            texture_key(memory, t) == inside, "compressed cache key excludes bytes outside its declared DXT payload");
    }
}
}
int main() {
    const bool checked = std::getenv("MHP3RD_CHECK_TEXTURE_DECODE") != nullptr;
    std::ostringstream diagnostics;
    auto *original_output = checked ? std::cout.rdbuf(diagnostics.rdbuf()) : nullptr;
    direct_formats();
    indexed_formats();
    palette_windows();
    rows_and_swizzle();
    compressed();
    mirrored_vram_boundary();
    invalid_and_keys();
    if (checked) {
        std::cout.rdbuf(original_output);
        std::cout << diagnostics.str();
        expect(diagnostics.str().find("[texture-check] 25 textures compared, 0 differed") != std::string::npos,
            "texture diagnostic reports its real comparison batch without any pixel mismatch");
    }
    std::cout << (failures ? "FAIL" : "PASS") << ": texture formats/snapshots/cache (" << failures << " failures)\n";
    return failures ? 1 : 0;
}
