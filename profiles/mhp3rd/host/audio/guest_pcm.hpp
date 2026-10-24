#pragma once

#include "psprecomp/guest_memory.hpp"

#include <cstdint>
#include <vector>

namespace mhp3rd::audio {

// Validate the entire guest range before allocating or reading a stereo copy.
// Invalid buffers leave the previous staging allocation untouched.
bool stage_guest_pcm(const psprecomp::GuestMemory &memory, std::uint32_t buffer, std::uint32_t frames, bool mono,
    std::vector<std::int16_t> &staging);

} // namespace mhp3rd::audio
