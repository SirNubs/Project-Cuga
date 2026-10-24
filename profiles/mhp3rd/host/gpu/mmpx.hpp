#pragma once

#include <cstdint>
#include <vector>

namespace mhp3rd::gpu::ui {

// MMPX (Morgan McGuire and Mara Gagiu, "MMPX Style-Preserving Pixel Art
// Magnification", Journal of Computer Graphics Techniques 10(2), 2021):
// doubles an RGBA8 image (red in the low byte, alpha in the high one),
// keeping its exact palette, transparency and single-pixel features, and
// shaping diagonals, curves and corners from each pixel's 5x5 neighbourhood.
// Written for this project from the rules the paper lists; see
// docs/SOURCE_PROVENANCE.md. Pixels beyond the edges repeat the edge.
void mmpx_2x(const std::uint32_t *in, std::uint32_t width, std::uint32_t height, std::vector<std::uint32_t> &out);

} // namespace mhp3rd::gpu::ui
