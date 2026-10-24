// Checked 16-bit image conversion, without allocating large images.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
std::size_t allocation_calls{};
std::size_t free_calls{};
std::size_t largest_request{};
void *bounded_allocate(std::size_t size) {
    ++allocation_calls;
    if (size > largest_request) largest_request = size;
    return size > 0u && size <= 1024u * 1024u ? std::malloc(size) : nullptr;
}
void bounded_free(void *data) {
    ++free_calls;
    std::free(data);
}
}

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STB_IMAGE_STATIC
#define STBI_MALLOC bounded_allocate
#define STBI_REALLOC std::realloc
#define STBI_FREE bounded_free
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace {
int failures{};
void check(bool condition, const char *message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
void rejected_conversion(unsigned int width, unsigned int height) {
    // The converter must reject before reading this deliberately tiny source.
    auto *source = static_cast<stbi__uint16 *>(std::malloc(sizeof(stbi__uint16)));
    check(source != nullptr, "synthetic source allocated");
    if (source == nullptr) return;
    allocation_calls = free_calls = 0;
    auto *result = stbi__convert_format16(source, 1, 4, width, height);
    check(result == nullptr, "oversized conversion rejected");
    check(allocation_calls == 0, "oversized conversion rejected before allocation");
    check(free_calls == 1, "rejected conversion frees its source");
    stbi_image_free(result);
}
void normal_conversion() {
    constexpr stbi__uint16 gray[] = {0u, 0x1234u, 0x8000u, 0xffffu};
    auto *source = static_cast<stbi__uint16 *>(std::malloc(sizeof(gray)));
    check(source != nullptr, "normal source allocated");
    if (source == nullptr) return;
    for (unsigned int i = 0; i < 4; ++i) source[i] = gray[i];
    auto *rgba = stbi__convert_format16(source, 1, 4, 2, 2);
    check(rgba != nullptr, "normal grayscale conversion succeeds");
    if (rgba != nullptr) {
        for (unsigned int i = 0; i < 4; ++i) {
            check(rgba[4 * i] == gray[i] && rgba[4 * i + 1] == gray[i] && rgba[4 * i + 2] == gray[i],
                "normal conversion preserves all 16 grayscale bits");
            check(rgba[4 * i + 3] == 0xffffu, "normal conversion fills opaque alpha");
        }
    }
    stbi_image_free(rgba);
}
void normal_png() {
    constexpr unsigned char png[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48,
        0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x10, 0x00, 0x00, 0x00, 0x00, 0x07, 0x4d, 0x8e,
        0xbb, 0x00, 0x00, 0x00, 0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0x60, 0x10, 0x32, 0x61, 0x68,
        0x60, 0xf8, 0xff, 0x1f, 0x00, 0x06, 0xbd, 0x02, 0xc5, 0xcb, 0xce, 0x3a, 0x38, 0x00, 0x00, 0x00, 0x00, 0x49,
        0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    int width = 0, height = 0, channels = 0;
    unsigned char *rgba = stbi_load_from_memory(png, sizeof(png), &width, &height, &channels, 4);
    check(rgba != nullptr && width == 2 && height == 2 && channels == 1,
        "normal 16-bit grayscale PNG decodes through the public 8-bit API");
    if (rgba != nullptr) {
        constexpr unsigned char gray[] = {0u, 0x12u, 0x80u, 0xffu};
        for (unsigned int i = 0; i < 4; ++i)
            check(rgba[4 * i] == gray[i] && rgba[4 * i + 1] == gray[i] && rgba[4 * i + 2] == gray[i] &&
                    rgba[4 * i + 3] == 255u,
                "normal PNG has expected RGBA bytes");
    }
    stbi_image_free(rgba);
}
}

int main() {
    // Before the fix these sizes requested 0 or a multi-gigabyte allocation.
    rejected_conversion(32768u, 16384u); // 4 GiB wraps to zero in 32-bit arithmetic.
    rejected_conversion(32768u, 8192u);  // 2 GiB exceeds the decoder's INT_MAX limit.
    rejected_conversion(std::numeric_limits<unsigned int>::max(), 1u);
    normal_conversion();
    normal_png();
    check(largest_request <= 1024u * 1024u, "tests never request a large allocation");
    if (failures != 0) return 1;
    std::cout << "image conversion tests passed\n";
    return 0;
}
