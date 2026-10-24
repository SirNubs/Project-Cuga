#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace mhp2g::game {
// Validate the complete list before editing any bytes. Prefixes each key
// title with the game's small ASCII star using existing padding only.
std::size_t mark_key_quests(std::span<std::uint8_t> bytes);
}
