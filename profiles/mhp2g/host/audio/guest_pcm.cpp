#include "audio/guest_pcm.hpp"

#include <bit>
#include <limits>

namespace mhp2g::audio {

bool stage_guest_pcm(const psprecomp::GuestMemory &memory, std::uint32_t buffer, std::uint32_t frames, bool mono,
    std::vector<std::int16_t> &staging) {
    const std::size_t count = frames;
    if (buffer == 0u || count == 0u || count > std::numeric_limits<std::size_t>::max() / 4u) return false;
    const std::size_t words = count * (mono ? 1u : 2u);
    const std::uint8_t *source = memory.raw_pointer(buffer, words * 2u);
    if (source == nullptr) return false;
    staging.resize(count * 2u);
    const auto sample_at = [&](std::size_t index) {
        const auto bits = static_cast<std::uint16_t>(source[index * 2u] | (source[index * 2u + 1u] << 8u));
        return std::bit_cast<std::int16_t>(bits);
    };
    for (std::size_t frame = 0; frame < count; ++frame) {
        const std::size_t index = mono ? frame : frame * 2u;
        staging[frame * 2u] = sample_at(index);
        staging[frame * 2u + 1u] = mono ? staging[frame * 2u] : sample_at(index + 1u);
    }
    return true;
}

} // namespace mhp2g::audio
