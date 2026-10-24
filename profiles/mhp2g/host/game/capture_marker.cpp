#include "game/capture_marker.hpp"
#include "settings/settings.hpp"
#include "psprecomp/runtime.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

#if defined(MHP2G_HUD_FUNCTIONS_INC)
namespace psprecomp {
#define MHP2G_HUD_FUNCTION(address, unit) void unit(Runtime &, AllegrexContext &);
#include MHP2G_HUD_FUNCTIONS_INC
#undef MHP2G_HUD_FUNCTION
}
#endif

namespace mhp2g::game {
namespace {
constexpr std::uint32_t kDrawMarker = 0x088446F4u;
constexpr std::uint32_t kQuest = 0x09A5A580u;
constexpr std::uint32_t kRatios = 0x09BB3D28u;
psprecomp::Runtime::RecompiledFunction original{};
bool hooked{};
std::vector<CaptureMarker> pending, shown;

bool supported(const psprecomp::GuestMemory &m) {
    constexpr char name[] = "game_task.ovl";
    return m.raw_pointer(kQuest, 64u) && m.load32(kQuest) == 0x336F574Du &&
        m.load32(kQuest + 8u) == kQuest &&
        std::memcmp(m.raw_pointer(kQuest + 32u, sizeof(name)), name, sizeof(name)) == 0 &&
        m.load32(kDrawMarker) == 0x27BDFF70u && m.load32(kDrawMarker + 4u) == 0xAFBF001Cu &&
        m.load32(0x09AD3620u) == 0x27BDFFE0u && m.load32(0x09AD3630u) == 0x908501E8u;
}

void draw_marker(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    const auto &m = runtime.memory();
    const auto monster = ctx.gpr[5];
    bool replaced = false;
    if (settings::current().capture_skull && supported(m) && monster >= 0x08800000u &&
        m.raw_pointer(monster, 0x4BDu)) {
        const auto kind = m.load8(monster + 0x1E8u);
        const auto ratio = kRatios + std::uint32_t(kind) * 0x1D0u;
        if (kind < 176u && m.raw_pointer(ratio, 4u)) {
            const CaptureState state{kind, static_cast<std::int16_t>(m.load16(monster + 0x2E4u)),
                static_cast<std::int16_t>(m.load16(monster + 0x41Eu)),
                static_cast<std::int8_t>(m.load8(monster + 0x4BCu)),
                std::bit_cast<float>(m.load32(ratio)), m.load8(monster + 0x3D0u) != 0u,
                settings::current().monster_map};
            const CaptureMarker marker{ctx.fpr[12], ctx.fpr[13], ctx.fpr[14]};
            if (capture_ready(state) && std::isfinite(marker.x) && std::isfinite(marker.y) &&
                std::isfinite(marker.scale) && marker.x >= 0 && marker.x <= 480 &&
                marker.y >= 0 && marker.y <= 272 && marker.scale > 0 && marker.scale <= 16) {
                // The original caller already applied map visibility, zoom and
                // clipping for this particular monster. Keep those coordinates.
                pending.push_back(marker);
                ctx.pc = ctx.gpr[31];
                replaced = true;
            }
        }
    }
    if (!replaced) original(runtime, ctx);
}
}

void capture_marker_frame(psprecomp::Runtime &runtime) {
    shown = std::move(pending);
    pending.clear();
    if (!settings::current().capture_skull || !supported(runtime.memory())) shown.clear();
    if (!original) {
#if defined(MHP2G_HUD_FUNCTIONS_INC)
#define MHP2G_HUD_FUNCTION(address, unit) if (address == kDrawMarker) original = &psprecomp::unit;
#include MHP2G_HUD_FUNCTIONS_INC
#undef MHP2G_HUD_FUNCTION
#endif
    }
    if (!hooked && original && runtime.memory().load32(kDrawMarker) == 0x27BDFF70u) {
        runtime.register_function(kDrawMarker, &draw_marker, "mhp2g_capture_map_marker");
        hooked = true;
        std::cout << "[qol] capture map marker hook installed" << std::endl;
    }
}
std::span<const CaptureMarker> capture_markers() { return shown; }
}
