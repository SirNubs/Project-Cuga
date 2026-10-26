#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mhp2g::gpu {

// Independently expressed from the legacy format's arithmetic semantics;
// see docs/MHP2G_TEXTURE_PACKS.md and SOURCE_PROVENANCE.md. No ISA-specific
// implementation or host-pointer alignment is part of this implementation.
[[nodiscard]] inline std::uint32_t texture_pack_quick(
    const std::uint8_t *bytes, std::size_t size, std::uint32_t guest_address = 0u) {
    const auto word = [bytes](std::size_t p) {
        return std::uint32_t(bytes[p]) | (std::uint32_t(bytes[p + 1u]) << 8u) |
               (std::uint32_t(bytes[p + 2u]) << 16u) | (std::uint32_t(bytes[p + 3u]) << 24u);
    };
    if ((guest_address & 15u) != 0u || (size & 63u) != 0u) {
        std::uint32_t result = 0u;
        for (std::size_t offset = 0; offset + 8u <= size; offset += 8u)
            result = (result + word(offset)) ^ word(offset + 4u);
        return result;
    }
    const auto add_halves = [](std::uint32_t a, std::uint32_t b) {
        return ((a + b) & 0xFFFFu) | (((a & 0xFFFF0000u) + (b & 0xFFFF0000u)) & 0xFFFF0000u);
    };
    const auto multiply_halves = [](std::uint32_t a, std::uint32_t b) {
        const auto low = std::uint32_t(std::uint64_t(a & 0xFFFFu) * (b & 0xFFFFu)) & 0xFFFFu;
        const auto high = std::uint32_t(std::uint64_t(a >> 16u) * (b >> 16u)) << 16u;
        return low | high;
    };
    // Two independent 16-bit weights in each little-endian word.
    constexpr std::array<std::uint32_t, 4> initial{0x9BD9C00Bu, 0xB6514B73u, 0x43094D9Bu, 0x00010083u};
    std::uint32_t result = 0u;
    // Evaluate each 32-bit lane as its own recurrence over 64-byte blocks.
    for (std::size_t lane = 0; lane < initial.size(); ++lane) {
        std::uint32_t value = 0u;
        std::uint32_t weight = initial[lane];
        for (std::size_t block = 0; block < size; block += 64u) {
            const std::size_t at = block + lane * 4u;
            value = add_halves(value, multiply_halves(word(at), weight));
            value = (value ^ word(at + 16u)) + word(at + 32u);
            value ^= multiply_halves(word(at + 48u), weight);
            weight = add_halves(weight, 0x24552455u);
        }
        result += value + weight;
    }
    return result;
}

} // namespace mhp2g::gpu
