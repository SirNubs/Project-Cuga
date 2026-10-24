// PNG size validation and allocation failure recovery, using only synthetic pixels.
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {
std::unordered_set<void *> live;
std::size_t requests{};
std::size_t fail_at{};
std::size_t largest_request{};
constexpr std::size_t kAllocationLimit = 1024u * 1024u;

bool reject(std::size_t size) {
    ++requests;
    largest_request = size > largest_request ? size : largest_request;
    return size > kAllocationLimit || (fail_at != 0 && requests == fail_at);
}
void *allocate(std::size_t size) {
    if (reject(size)) return nullptr;
    void *p = std::malloc(size);
    if (p != nullptr) live.insert(p);
    return p;
}
void *resize(void *p, std::size_t size) {
    if (reject(size)) return nullptr;
    if (p != nullptr) live.erase(p);
    void *result = std::realloc(p, size);
    if (result != nullptr)
        live.insert(result);
    else if (p != nullptr)
        live.insert(p);
    return result;
}
void release(void *p) {
    if (p != nullptr) live.erase(p);
    std::free(p);
}
}

#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#define STBIW_MALLOC allocate
#define STBIW_REALLOC resize
#define STBIW_FREE release
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STB_IMAGE_STATIC
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
void reset(std::size_t fail = 0) {
    check(live.empty(), "no writer allocation leaked");
    requests = largest_request = 0;
    fail_at = fail;
}
void oversized() {
    unsigned char pixel = 0;
    int length = 123;
    reset();
    auto *png = stbi_write_png_to_mem(&pixel, 0, INT_MAX, 2, 4, &length);
    check(png == nullptr && requests == 0, "overflowing PNG row rejected before allocation");
    release(png);
    reset();
    png = stbi_write_png_to_mem(&pixel, 0, 32768, 32768, 4, &length);
    check(png == nullptr && requests == 0, "overflowing filtered image rejected before allocation");
    release(png);
}
void invalid_inputs() {
    unsigned char pixel = 0;
    int length = 0;
    for (const auto &sizes :
        std::vector<std::vector<int>>{{0, 1, 1}, {1, 0, 1}, {-1, 1, 1}, {1, -1, 1}, {1, 1, 0}, {1, 1, 5}}) {
        reset();
        check(stbi_write_png_to_mem(&pixel, 0, sizes[0], sizes[1], sizes[2], &length) == nullptr && requests == 0,
            "invalid dimensions or component count rejected before allocation");
    }
    reset();
    check(stbi_write_png_to_mem(nullptr, 0, 1, 1, 1, &length) == nullptr && requests == 0, "null pixels rejected");
    check(
        stbi_write_png_to_mem(&pixel, 0, 1, 1, 1, nullptr) == nullptr && requests == 0, "null output length rejected");
    check(stbi_write_png_to_mem(&pixel, INT_MIN, 1, 2, 1, &length) == nullptr && requests == 0,
        "unrepresentable negated stride rejected");
    check(stbi_zlib_compress(&pixel, INT_MAX, &length, 8) == nullptr && requests == 0,
        "unrepresentable stored deflate size rejected before allocation");
    check(stbi_zlib_compress(&pixel, 1, &length, INT_MAX) == nullptr && requests == 0,
        "overflowing compression quality rejected before allocation");
    check(stbi_zlib_compress(&pixel, -1, &length, 8) == nullptr && requests == 0,
        "negative deflate input length rejected");
}
void buffer_bounds() {
    reset();
    void *p = nullptr;
    check(stbiw__sbgrowf(&p, INT_MAX, 1) == nullptr && p == nullptr && requests == 0,
        "overflowing initial buffer growth rejected");
    int header[] = {INT_MAX, INT_MAX, 0};
    p = header + 2;
    void *before = p;
    check(stbiw__sbgrowf(&p, 1, 1) == nullptr && p == before && requests == 0,
        "overflowing existing capacity rejected without losing old buffer");
    p = nullptr;
    check(stbiw__sbgrowf(&p, 1, sizeof(unsigned char *)) != nullptr, "small buffer grows");
    before = p;
    fail_at = requests + 1;
    check(stbiw__sbgrowf(&p, 20, sizeof(unsigned char *)) == nullptr && p == before,
        "failed resize leaves old buffer owned by caller");
    release(stbiw__sbraw(p));
}
void round_trip(int components, int filter, bool flip, bool negative_stride) {
    constexpr int width = 7, height = 3;
    const int stride = width * components + 5;
    std::vector<unsigned char> source(stride * height, 0xcc);
    for (int row = 0; row < height; ++row)
        for (int col = 0; col < width * components; ++col)
            source[row * stride + col] = static_cast<unsigned char>(row * 71 + col * 19);
    stbi_write_force_png_filter = filter;
    stbi_flip_vertically_on_write(flip);
    reset();
    int length = 0;
    unsigned char *png = stbi_write_png_to_mem(source.data() + (negative_stride ? (height - 1) * stride : 0),
        negative_stride ? -stride : stride, width, height, components, &length);
    check(png != nullptr && length > 0, "small PNG with explicit stride is written");
    int x = 0, y = 0, n = 0;
    unsigned char *decoded = png != nullptr ? stbi_load_from_memory(png, length, &x, &y, &n, components) : nullptr;
    check(decoded != nullptr && x == width && y == height && n == components, "PNG reads back at expected shape");
    if (decoded != nullptr)
        for (int row = 0; row < height; ++row) {
            const int source_row = flip != negative_stride ? height - 1 - row : row;
            check(std::memcmp(
                      decoded + row * width * components, source.data() + source_row * stride, width * components) == 0,
                "PNG preserves pixels and orientation");
        }
    stbi_image_free(decoded);
    release(png);
    check(live.empty(), "successful PNG frees all writer allocations");
    stbi_flip_vertically_on_write(0);
    stbi_write_force_png_filter = -1;
}
void allocation_failures() {
    constexpr int width = 37, height = 13;
    std::vector<unsigned char> pixels(width * height * 4);
    unsigned state = 123456789;
    for (auto &pixel : pixels) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        pixel = static_cast<unsigned char>(state);
    }
    reset();
    int length = 0;
    auto *png = stbi_write_png_to_mem(pixels.data(), 0, width, height, 4, &length);
    check(png != nullptr, "allocation-failure fixture normally succeeds");
    const std::size_t allocations = requests;
    release(png);
    for (std::size_t failed_request = 1; failed_request <= allocations; ++failed_request) {
        reset(failed_request);
        png = stbi_write_png_to_mem(pixels.data(), 0, width, height, 4, &length);
        check(png == nullptr, "every failed PNG allocation returns failure");
        release(png);
        check(live.empty(), "every PNG allocation failure frees temporary buffers");
    }
    reset();
    unsigned char byte = 0;
    auto *compressed = stbi_zlib_compress(&byte, 0, &length, 8);
    check(compressed != nullptr, "empty deflate stream remains supported");
    release(compressed);
    check(live.empty(), "empty deflate stream cleans up");
}
}
int main(int argc, char **argv) {
    oversized();
    if (argc == 2 && std::string_view(argv[1]) == "--oversized-only") return failures != 0;
    invalid_inputs();
    buffer_bounds();
    for (int components = 1; components <= 4; ++components)
        for (int filter = -1; filter <= 4; ++filter)
            for (bool flip : {false, true})
                for (bool negative_stride : {false, true}) round_trip(components, filter, flip, negative_stride);
    allocation_failures();
    if (failures != 0) return 1;
    std::cout << "PNG size tests passed\n";
    return 0;
}
