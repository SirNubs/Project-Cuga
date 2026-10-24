#pragma once
#include <cstdint>
#include <span>
namespace psprecomp { class Runtime; }
namespace mhp2g::game {
struct CaptureState {
    std::uint8_t kind{};
    std::int16_t health{}, maximum{};
    std::int8_t strength{};
    float base_ratio{};
    bool painted{};
    bool map_revealed{}; // Monsters on the map QOL also reveals eligible markers
};
bool capture_ready(const CaptureState &state);
struct CaptureMarker { float x{}, y{}, scale{}; };
void capture_marker_frame(psprecomp::Runtime &runtime);
std::span<const CaptureMarker> capture_markers();
}
