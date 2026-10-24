#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mhp2g::mods {
// A raw UMD read can straddle DATA.BIN. Overlay only the archive bytes;
// preserve neighbouring disc files and propagate a short archive read.
template <typename Reader>
std::size_t overlay_archive_read(std::uint64_t absolute, std::span<std::uint8_t> bytes,
    std::uint64_t archive_start, std::uint64_t archive_size, Reader read) {
    const auto prefix = absolute < archive_start
        ? static_cast<std::size_t>(std::min<std::uint64_t>(archive_start - absolute, bytes.size())) : 0u;
    if (prefix == bytes.size()) return bytes.size();
    const auto offset = absolute < archive_start ? 0u : absolute - archive_start;
    if (offset >= archive_size) return bytes.size();
    const auto amount = static_cast<std::size_t>(std::min<std::uint64_t>(archive_size - offset, bytes.size() - prefix));
    const auto got = std::min<std::size_t>(read(offset, bytes.subspan(prefix, amount)), amount);
    return got == amount ? bytes.size() : prefix + got;
}
}
