#include "game/capture_marker.hpp"
#include <cmath>
namespace mhp2g::game {
bool capture_ready(const CaptureState &s) {
    if ((!s.painted && !s.map_revealed) || s.health <= 0 || s.maximum <= 0 || s.health > s.maximum ||
        !std::isfinite(s.base_ratio) || s.base_ratio <= 0.0f || s.base_ratio > 0.8f) return false;
    // The exclusions and float/integer rounding below match ULJM05500's
    // capture predicate at 0x09AD3620; this is not a general HP percentage.
    switch (s.kind) {
    case 54: case 59: case 60: case 64: case 65: case 76: case 79: case 80: case 88:
        return false;
    default: break;
    }
    float ratio = s.base_ratio + 0.1f;
    if (s.strength < 0 || s.strength >= 24) ratio *= 0.5f;
    else if (s.strength >= 16) ratio *= 0.6f;
    const int threshold = static_cast<int>(static_cast<float>(s.maximum) * ratio);
    return s.health <= threshold;
}
}
