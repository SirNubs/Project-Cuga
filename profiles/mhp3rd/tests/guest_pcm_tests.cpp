#include "audio/guest_pcm.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    psprecomp::GuestMemory memory;
    constexpr std::uint32_t size = 32u * 1024u * 1024u;
    constexpr std::uint32_t start = psprecomp::GuestMemory::kPhysicalBase;
    memory.store16(start, 0x8000u);
    memory.store16(start + 2u, 0x7FFFu);
    memory.store16(start + 4u, 0xFFFFu);
    memory.store16(start + 6u, 0u);
    int failures = 0;
    const auto check = [&](bool value, const char *what) {
        if (!value) {
            std::cerr << "FAIL: " << what << '\n';
            ++failures;
        }
    };
    std::vector<std::int16_t> staging;
    check(mhp3rd::audio::stage_guest_pcm(memory, start, 2u, false, staging), "mapped stereo buffer accepted");
    check(
        staging == std::vector<std::int16_t>({-32768, 32767, -1, 0}), "stereo channel order and signed PCM preserved");
    check(mhp3rd::audio::stage_guest_pcm(memory, start, 2u, true, staging), "mapped mono buffer accepted");
    check(staging == std::vector<std::int16_t>({-32768, -32768, 32767, 32767}), "mono samples duplicated to stereo");
    const auto previous = staging;
    for (const std::uint32_t frames : {0u, 0x40000000u, 0x80000000u, 0xFFFFFFFFu}) {
        for (const bool mono : {false, true}) {
            check(!mhp3rd::audio::stage_guest_pcm(memory, start, frames, mono, staging),
                "wrapped or zero-sized request rejected");
            check(staging == previous, "invalid counts do not allocate or alter staging");
        }
    }
    check(!mhp3rd::audio::stage_guest_pcm(memory, start + size - 2u, 2u, false, staging),
        "buffer crossing mapped boundary rejected");
    check(!mhp3rd::audio::stage_guest_pcm(memory, 0u, 2u, false, staging), "null guest buffer rejected");
    check(staging == previous, "invalid ranges do not alter staging");
    memory.store16(start + size - 2u, 0x1234u);
    check(
        mhp3rd::audio::stage_guest_pcm(memory, start + size - 2u, 1u, true, staging), "exact-end mono range accepted");
    check(staging == std::vector<std::int16_t>({0x1234, 0x1234}), "exact-end sample decoded");
    return failures ? 1 : 0;
}
