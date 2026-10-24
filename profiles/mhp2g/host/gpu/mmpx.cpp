// MMPX, written for this project from the rules in McGuire and Gagiu, "MMPX
// Style-Preserving Pixel Art Magnification", JCGT 10(2), 2021 (Listing 4 of
// the paper and the text around it). The authors' reference code is under the
// MIT license and was not copied; see docs/SOURCE_PROVENANCE.md.
#include "gpu/mmpx.hpp"

#include <algorithm>

namespace mhp2g::gpu::ui {

void mmpx_2x(const std::uint32_t *in, std::uint32_t width, std::uint32_t height, std::vector<std::uint32_t> &out) {
    out.assign(static_cast<std::size_t>(width) * height * 4u, 0u);
    const auto src = [&](std::int64_t x, std::int64_t y) {
        x = std::clamp<std::int64_t>(x, 0, static_cast<std::int64_t>(width) - 1);
        y = std::clamp<std::int64_t>(y, 0, static_cast<std::int64_t>(height) - 1);
        return in[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)];
    };
    // Dark or opaque colours count as the foreground where a rule has to
    // choose: the sum of the channels plus one, times 256 minus alpha.
    const auto luma = [](std::uint32_t c) {
        return ((c & 0xFFu) + ((c >> 8u) & 0xFFu) + ((c >> 16u) & 0xFFu) + 1u) * (256u - (c >> 24u));
    };
    const auto all_eq2 = [](std::uint32_t b, std::uint32_t a0, std::uint32_t a1) { return b == a0 && b == a1; };
    const auto all_eq3 = [](std::uint32_t b, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2) {
        return b == a0 && b == a1 && b == a2;
    };
    const auto all_eq4 = [](std::uint32_t b, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3) {
        return b == a0 && b == a1 && b == a2 && b == a3;
    };
    const auto any_eq3 = [](std::uint32_t b, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2) {
        return b == a0 || b == a1 || b == a2;
    };
    const auto none_eq2 = [](std::uint32_t b, std::uint32_t a0, std::uint32_t a1) { return b != a0 && b != a1; };
    const auto none_eq4 = [](std::uint32_t b, std::uint32_t a0, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3) {
        return b != a0 && b != a1 && b != a2 && b != a3;
    };
    const std::size_t out_width = static_cast<std::size_t>(width) * 2u;
    for (std::int64_t y = 0; y < static_cast<std::int64_t>(height); ++y) {
        for (std::int64_t x = 0; x < static_cast<std::int64_t>(width); ++x) {
            //       P
            //     A B C
            //   Q D E F R
            //     G H I
            //       S
            const std::uint32_t A = src(x - 1, y - 1), B = src(x, y - 1), C = src(x + 1, y - 1);
            const std::uint32_t D = src(x - 1, y), E = src(x, y), F = src(x + 1, y);
            const std::uint32_t G = src(x - 1, y + 1), H = src(x, y + 1), I = src(x + 1, y + 1);
            // The four pixels E becomes: J K on top, L M below.
            std::uint32_t J = E, K = E, L = E, M = E;
            // Where all eight neighbours are E there is nothing to shape.
            if (!(A == E && B == E && C == E && D == E && F == E && G == E && H == E && I == E)) {
                const std::uint32_t P = src(x, y - 2), S = src(x, y + 2);
                const std::uint32_t Q = src(x - 2, y), R = src(x + 2, y);
                const std::uint32_t Bl = luma(B), Dl = luma(D), El = luma(E), Fl = luma(F), Hl = luma(H);

                // Edges at 45 degrees: fill the corner along the edge, on the
                // darker or more opaque side only, keeping sharp corners and
                // single-pixel bumps.
                if ((D == B && D != H && D != F) && (El >= Dl || E == A) && any_eq3(E, A, C, G) &&
                    (El < Dl || A != D || E != P || E != Q))
                    J = D;
                if ((B == F && B != D && B != H) && (El >= Bl || E == C) && any_eq3(E, A, C, I) &&
                    (El < Bl || C != B || E != P || E != R))
                    K = B;
                if ((H == D && H != F && H != B) && (El >= Hl || E == G) && any_eq3(E, A, G, I) &&
                    (El < Hl || G != H || E != S || E != Q))
                    L = H;
                if ((F == H && F != B && F != D) && (El >= Fl || E == I) && any_eq3(E, C, G, I) &&
                    (El < Fl || I != H || E != R || E != S))
                    M = F;

                // Where two lines cross, joined unless it is the edge of a
                // checkerboard dither.
                if ((E != F && all_eq4(E, C, I, D, Q) && all_eq2(F, B, H)) && (F != src(x + 3, y))) K = M = F;
                if ((E != D && all_eq4(E, A, G, F, R) && all_eq2(D, B, H)) && (D != src(x - 3, y))) J = L = D;
                if ((E != H && all_eq4(E, G, I, B, P) && all_eq2(H, D, F)) && (H != src(x, y + 3))) L = M = H;
                if ((E != B && all_eq4(E, A, C, H, S) && all_eq2(B, D, F)) && (B != src(x, y - 3))) J = K = B;

                // The tips of triangles, which the edge rules would flatten.
                if (Bl < El && all_eq4(E, G, H, I, S) && none_eq4(E, A, D, C, F)) J = K = B;
                if (Hl < El && all_eq4(E, A, B, C, P) && none_eq4(E, D, G, I, F)) L = M = H;
                if (Fl < El && all_eq4(E, A, D, G, Q) && none_eq4(E, B, C, I, H)) K = M = F;
                if (Dl < El && all_eq4(E, C, F, I, R) && none_eq4(E, B, A, G, H)) J = L = D;

                // Edges two pixels across for one down (and the other seven
                // ways round): one more output pixel copied along them.
                if (H != B) {
                    if (H != A && H != E && H != C) {
                        if (all_eq3(H, G, F, R) && none_eq2(H, D, src(x + 2, y - 1))) L = M;
                        if (all_eq3(H, I, D, Q) && none_eq2(H, F, src(x - 2, y - 1))) M = L;
                    }
                    if (B != I && B != G && B != E) {
                        if (all_eq3(B, A, F, R) && none_eq2(B, D, src(x + 2, y + 1))) J = K;
                        if (all_eq3(B, C, D, Q) && none_eq2(B, F, src(x - 2, y + 1))) K = J;
                    }
                }
                if (F != D) {
                    if (D != I && D != E && D != C) {
                        if (all_eq3(D, A, H, S) && none_eq2(D, B, src(x + 1, y + 2))) J = L;
                        if (all_eq3(D, G, B, P) && none_eq2(D, H, src(x + 1, y - 2))) L = J;
                    }
                    if (F != E && F != A && F != G) {
                        if (all_eq3(F, C, H, S) && none_eq2(F, B, src(x - 1, y + 2))) K = M;
                        if (all_eq3(F, I, B, P) && none_eq2(F, H, src(x - 1, y - 2))) M = K;
                    }
                }
            }
            const std::size_t at = static_cast<std::size_t>(y) * 2u * out_width + static_cast<std::size_t>(x) * 2u;
            out[at] = J;
            out[at + 1u] = K;
            out[at + out_width] = L;
            out[at + out_width + 1u] = M;
        }
    }
}

} // namespace mhp2g::gpu::ui
