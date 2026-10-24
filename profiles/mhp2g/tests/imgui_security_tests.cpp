// Include the vendor implementation to exercise its private debug formatter.
#include "../third_party/imgui/imgui.cpp"

#include <cstring>
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <limits>
#include <string>
#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
int failures{};
ImTextureData *watched_texture{};
bool forbid_allocation{};
bool fail_allocation{};
int allocation_calls{};
std::size_t largest_request{};
void check(bool condition, const char *message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
bool unpublished() {
    return watched_texture && watched_texture->Pixels == nullptr && watched_texture->Width == 0 &&
        watched_texture->Height == 0 && watched_texture->Status == ImTextureStatus_Destroyed;
}
void *bounded_allocate(std::size_t size, void *) {
    ++allocation_calls;
    if (forbid_allocation) std::_Exit(99);
    if (size > largest_request) largest_request = size;
    if (fail_allocation) return nullptr;
    return size <= 1024u * 1024u ? std::malloc(size) : nullptr;
}
void bounded_free(void *data, void *) {
    std::free(data);
}
void rejected_abort(int) {
    std::_Exit(unpublished() && allocation_calls == (fail_allocation ? 1 : 0) ? 86 : 87);
}
[[noreturn]] void rejected_texture(int mode) {
#ifdef _MSC_VER
    // The Microsoft Debug CRT displays a modal dialog before raising SIGABRT.
    // Expected fatal cases must reach our handler without unattended UI/reporting.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    std::signal(SIGABRT, rejected_abort);
    ImGui::SetAllocatorFunctions(bounded_allocate, bounded_free);
    // The signal handler watches this object until the child calls _Exit.
    static ImTextureData tex;
    watched_texture = &tex;
    forbid_allocation = mode < 6;
    fail_allocation = mode >= 6;
    switch (mode) {
    case 0:
        tex.Create(ImTextureFormat_Alpha8, 46341, 46341);
        break;
    case 1:
        tex.Create(ImTextureFormat_RGBA32, 32768, 16384);
        break;
    case 2:
        tex.Create(ImTextureFormat_RGBA32, std::numeric_limits<int>::max(), 2);
        break;
    case 3:
        tex.Create(ImTextureFormat_RGBA32, 0, 4);
        break;
    case 4:
        tex.Create(ImTextureFormat_Alpha8, -1, 4);
        break;
    case 5:
        tex.Create(ImTextureFormat_RGBA32, std::numeric_limits<int>::max() / 4 + 1, 1);
        break;
    case 6:
        tex.Create(ImTextureFormat_RGBA32, 2, 2);
        break;
    case 7:
        tex.Create(ImTextureFormat_Alpha8, std::numeric_limits<int>::max(), 1);
        break;
    case 8:
        tex.Create(ImTextureFormat_RGBA32, std::numeric_limits<int>::max() / 4, 1);
        break;
    }
    std::_Exit(98); // A failed void creation must never return to pixel-writing callers.
}
bool rejects_texture(const char *executable, int mode) {
#ifdef _WIN32
    const std::string mode_text = std::to_string(mode);
    return _spawnl(_P_WAIT, executable, executable, "--reject-texture", mode_text.c_str(),
               static_cast<char *>(nullptr)) == 86;
#else
    (void)executable;
    const pid_t pid = fork();
    if (pid == 0) rejected_texture(mode);
    if (pid < 0) return false;
    int status{};
    return waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 86;
#endif
}
void texture_checks() {
    ImGuiMemAllocFunc old_alloc;
    ImGuiMemFreeFunc old_free;
    void *old_user_data;
    ImGui::GetAllocatorFunctions(&old_alloc, &old_free, &old_user_data);
    ImGui::SetAllocatorFunctions(bounded_allocate, bounded_free);
    {
        ImTextureData rgba;
        rgba.Create(ImTextureFormat_RGBA32, 4, 3);
        check(rgba.GetSizeInBytes() == 48 && rgba.GetPitch() == 16, "RGBA size and pitch");
        check(rgba.GetPixelsAt(3, 2) == rgba.Pixels + 44, "last RGBA pixel address");
        for (int i = 0; i < rgba.GetSizeInBytes(); ++i) check(rgba.Pixels[i] == 0, "texture zero initialized");
        ImTextureData destination;
        destination.Create(ImTextureFormat_RGBA32, 6, 5);
        ImFontAtlasTextureBlockFill(&rgba, 1, 1, 2, 2, 0x12345678);
        ImFontAtlasTextureBlockCopy(&rgba, 1, 1, &destination, 3, 2, 2, 2);
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 6; ++x)
                check(*static_cast<ImU32 *>(destination.GetPixelsAt(x, y)) ==
                        ((x >= 3 && x < 5 && y >= 2 && y < 4) ? 0x12345678u : 0u),
                    "row copy preserves guards");
        ImTextureData alpha;
        alpha.Create(ImTextureFormat_Alpha8, 7, 3);
        check(alpha.GetSizeInBytes() == 21 && alpha.GetPixelsAt(6, 2) == alpha.Pixels + 20,
            "Alpha8 size and pixel address");
        ImFontAtlas atlas;
        atlas.AddFontDefault();
        check(atlas.Build(), "default font atlas builds with bounded allocator");
        check(atlas.TexData && atlas.TexData->GetSizeInBytes() > 0 && atlas.TexData->GetSizeInBytes() <= 1024 * 1024,
            "default glyph rasterization uses bounded texture");
        const int packed_count = atlas.Builder->RectsPackedCount;
        const ImVec2i max_rect = atlas.Builder->MaxRectSize;
        check(ImFontAtlasPackAddRect(&atlas, 0, 1) == ImFontAtlasRectId_Invalid, "zero rectangle rejected");
        check(ImFontAtlasPackAddRect(&atlas, -1, 1) == ImFontAtlasRectId_Invalid, "negative rectangle rejected");
        check(ImFontAtlasPackAddRect(&atlas, 1, 65536) == ImFontAtlasRectId_Invalid,
            "oversized rectangle rejected before narrowing");
        const int old_padding = atlas.TexGlyphPadding;
        atlas.TexGlyphPadding = std::numeric_limits<int>::max();
        check(ImFontAtlasPackAddRect(&atlas, 2, 2) == ImFontAtlasRectId_Invalid, "padding addition overflow rejected");
        atlas.TexGlyphPadding = -1;
        check(ImFontAtlasPackAddRect(&atlas, 2, 2) == ImFontAtlasRectId_Invalid, "negative padding rejected");
        atlas.TexGlyphPadding = old_padding;
        check(atlas.Builder->RectsPackedCount == packed_count && atlas.Builder->MaxRectSize.x == max_rect.x &&
                atlas.Builder->MaxRectSize.y == max_rect.y,
            "rejected rectangle preserves packing state");
        const ImFontAtlasRectId packed = ImFontAtlasPackAddRect(&atlas, 4, 3);
        check(packed != ImFontAtlasRectId_Invalid, "normal custom rectangle packs");
        if (packed != ImFontAtlasRectId_Invalid) {
            const ImTextureRect *rect = ImFontAtlasPackGetRect(&atlas, packed);
            check(rect->w == 4 && rect->h == 3 && rect->x + rect->w <= atlas.TexData->Width &&
                    rect->y + rect->h <= atlas.TexData->Height,
                "packed rectangle lies inside allocated texture");
        }
    }
    check(largest_request <= 1024u * 1024u, "normal tests never request a large allocation");
    ImGui::SetAllocatorFunctions(old_alloc, old_free, old_user_data);
}
}

int main(int argc, char **argv) {
    if (argc == 3 && std::strcmp(argv[1], "--reject-texture") == 0) rejected_texture(std::atoi(argv[2]));
    for (int mode = 0; mode <= 8; ++mode)
        check(rejects_texture(argv[0], mode), "invalid size or OOM rejected before publishing texture");
    texture_checks();
    char buffer[64]{};
    FormatTextureRefForDebugDisplay(buffer, sizeof(buffer), ImTextureRef(ImTextureID(0x123456789ABCDEF0ULL)));
    if (std::strcmp(buffer, "0x123456789ABCDEF0") != 0) {
        std::cerr << "64-bit texture ID was truncated: " << buffer << '\n';
        return 1;
    }
    FormatTextureRefForDebugDisplay(buffer, sizeof(buffer), ImTextureRef(ImTextureID(0x1234ULL)));
    if (std::strcmp(buffer, "0x1234") != 0) return 1;
    char small[5]{};
    FormatTextureRefForDebugDisplay(small, sizeof(small), ImTextureRef(ImTextureID(0x123456789ABCDEF0ULL)));
    if (small[4] != '\0') return 1;
    std::cout << "ImGui texture security tests passed\n";
    return failures == 0 ? 0 : 1;
}
