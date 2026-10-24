#include "game/key_quest_archive.hpp"

#include "game/key_quests.hpp"
#include "mods/mhp2g_data_bin.hpp"

namespace mhp2g::game {
std::optional<KeyQuestFiles> key_quest_files(std::size_t entries, bool plain) {
    if (entries == 6602u && plain) return KeyQuestFiles{5310u, 5320u};
    if (entries == 6657u) return KeyQuestFiles{5342u, 5352u};
    return std::nullopt;
}

std::size_t mark_stored_key_quests(std::span<std::uint8_t> bytes, std::uint32_t block) {
    mods::p3rd::decrypt(bytes, block, 0u);
    const auto count = mark_key_quests(bytes);
    mods::p3rd::encrypt(bytes, block, 0u);
    return count;
}
}
