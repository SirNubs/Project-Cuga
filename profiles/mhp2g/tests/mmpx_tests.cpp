// MMPX, the 2D interface's pixel-art upscaler: what the paper says it keeps
// (the palette, flat areas, single pixels) and what it shapes (1:1 edges).

#include "gpu/mmpx.hpp"

#include <cstdint>
#include <iostream>
#include <random>
#include <set>
#include <vector>

namespace {

using mhp3rd::gpu::ui::mmpx_2x;

int failures{};

void expect(bool condition, const char *what) {
    if (condition) return;
    ++failures;
    std::cout << "FAIL: " << what << "\n";
}

constexpr std::uint32_t kWhite = 0xFFFFFFFFu;
constexpr std::uint32_t kBlack = 0xFF000000u;
constexpr std::uint32_t kRed = 0xFF0000FFu;

std::uint32_t at(const std::vector<std::uint32_t> &image, std::uint32_t width, std::uint32_t x, std::uint32_t y) {
    return image[static_cast<std::size_t>(y) * width + x];
}

void test_flat() {
    const std::vector<std::uint32_t> in(6u * 4u, kRed);
    std::vector<std::uint32_t> out;
    mmpx_2x(in.data(), 6u, 4u, out);
    expect(out.size() == 12u * 8u, "the output is twice as wide and twice as tall");
    bool flat = true;
    for (const std::uint32_t pixel : out) flat = flat && pixel == kRed;
    expect(flat, "a flat image stays flat");
}

void test_single_pixel() {
    std::vector<std::uint32_t> in(5u * 5u, kWhite);
    in[2u * 5u + 2u] = kRed;
    std::vector<std::uint32_t> out;
    mmpx_2x(in.data(), 5u, 5u, out);
    std::size_t red = 0;
    for (const std::uint32_t pixel : out) red += pixel == kRed ? 1u : 0u;
    expect(red == 4u, "a single pixel stays one pixel, twice as large");
    expect(at(out, 10u, 4u, 4u) == kRed && at(out, 10u, 5u, 5u) == kRed, "in its own place");
}

void test_palette() {
    std::mt19937 random(7u);
    const std::uint32_t colours[] = {kWhite, kBlack, kRed, 0x00000000u};
    std::vector<std::uint32_t> in(16u * 16u);
    for (std::uint32_t &pixel : in) pixel = colours[random() % 4u];
    std::vector<std::uint32_t> out;
    mmpx_2x(in.data(), 16u, 16u, out);
    const std::set<std::uint32_t> palette(std::begin(colours), std::end(colours));
    bool kept = true;
    for (const std::uint32_t pixel : out) kept = kept && palette.count(pixel) != 0u;
    expect(kept, "no colour is made that the image did not have, transparency included");
}

void test_diagonal() {
    // A black line at 45 degrees on white: the steps beside it are filled
    // half a source pixel out, so the line is smooth rather than a staircase.
    constexpr std::uint32_t kSize = 8u;
    std::vector<std::uint32_t> in(kSize * kSize, kWhite);
    for (std::uint32_t i = 0; i < kSize; ++i) in[i * kSize + i] = kBlack;
    std::vector<std::uint32_t> out;
    mmpx_2x(in.data(), kSize, kSize, out);
    bool filled = true;
    for (std::uint32_t i = 1; i + 2u < kSize; ++i) {
        // The white pixel right of (i, i): its bottom-left quarter.
        filled = filled && at(out, kSize * 2u, 2u * (i + 1u), 2u * i + 1u) == kBlack;
        // The white pixel below (i, i): its top-right quarter.
        filled = filled && at(out, kSize * 2u, 2u * i + 1u, 2u * (i + 1u)) == kBlack;
    }
    expect(filled, "a 1:1 edge has its steps filled");
    // Away from the edges, which repeat outwards.
    bool line = true;
    for (std::uint32_t i = 1; i + 1u < kSize; ++i)
        line = line && at(out, kSize * 2u, 2u * i, 2u * i) == kBlack &&
            at(out, kSize * 2u, 2u * i + 1u, 2u * i + 1u) == kBlack;
    expect(line, "and the line itself stays");
}

} // namespace

int main() {
    test_flat();
    test_single_pixel();
    test_palette();
    test_diagonal();
    if (failures != 0) {
        std::cout << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "mmpx tests passed\n";
    return 0;
}
