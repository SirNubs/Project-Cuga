#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace mhp2g::game {
struct KeyQuestFiles {
    std::uint32_t first;
    std::uint32_t last;
};
// Only archive layouts whose quest-summary lists have been verified.
std::optional<KeyQuestFiles> key_quest_files(std::size_t entries, bool plain);
// Preserve the archive's storage representation, including on validation failure.
std::size_t mark_stored_key_quests(std::span<std::uint8_t> bytes, std::uint32_t block);
}
