#include "game/key_quests.hpp"
#include <algorithm>
#include <vector>

namespace mhp2g::game {
std::size_t mark_key_quests(std::span<std::uint8_t> bytes) {
    const auto u32 = [&](std::size_t at) {
        return std::uint32_t(bytes[at]) | std::uint32_t(bytes[at + 1]) << 8 |
            std::uint32_t(bytes[at + 2]) << 16 | std::uint32_t(bytes[at + 3]) << 24;
    };
    if (bytes.size() < 8) return 0;
    const auto first = u32(0);
    if (first < 8 || first > 2048 || first > bytes.size() || first % 4 || u32(first - 4)) return 0;
    struct Title { std::size_t start, end; };
    std::vector<Title> titles;
    for (std::size_t pos = 0; pos + 4 < first; pos += 4) {
        const auto at = u32(pos);
        const std::size_t end = pos + 8 < first ? u32(pos + 4) : bytes.size();
        if (at < first || at + 0x34u >= end || end > bytes.size()) return 0;
        if (!(u32(at) & 0x04000000u)) continue;
        const auto start = at + 0x34u;
        auto stop = start;
        while (stop < end && bytes[stop]) {
            if (bytes[stop] < 32 || bytes[stop] > 126) return 0;
            ++stop;
        }
        if (stop == start || stop + 1 >= end || bytes[stop + 1]) return 0;
        if (bytes[start] == '*') continue; // another compatible label mod already marked it
        titles.push_back({start, stop});
    }
    for (const auto &title : titles) {
        std::move_backward(bytes.begin() + title.start, bytes.begin() + title.end, bytes.begin() + title.end + 1);
        bytes[title.start] = '*';
    }
    return titles.size();
}
}
