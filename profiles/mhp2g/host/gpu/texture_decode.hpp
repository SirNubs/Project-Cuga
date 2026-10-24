#pragma once

#include "ge_state.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace mhp2g::gpu {

// Decodes a PSP texture into RGBA8888 (one std::uint32_t per texel, red in the
// low byte). Handles the direct colour formats, 4/8/16/32-bit palettes, DXT1/3/5
// and the swizzled layout. Returns false when the texture cannot be read.
bool decode_texture(const GuestMemory &memory, const TextureState &texture, std::vector<std::uint32_t> &out);

// What decoding a texture reads from guest memory, copied, so that it can be
// decoded on another thread while the game goes on changing memory.
struct TextureSnapshot {
    TextureState texture;
    std::uint32_t row_bytes{};
    std::vector<std::uint8_t> texels;
    std::vector<std::uint8_t> clut; // the palette's first 512 entries, for the indexed formats
};
// Copies what decode_texture() would read; false for a texture it cannot copy
// that way (a block format, a run not contiguous in host memory), which is
// then decoded at once.
bool snapshot_texture(const GuestMemory &memory, const TextureState &texture, TextureSnapshot &snapshot);
// Decodes a snapshot to the texels decode_texture() gives. Safe on any thread;
// unswizzles the snapshot's texels in place.
bool decode_snapshot(TextureSnapshot &snapshot, std::vector<std::uint32_t> &out);

// "Xbox button prompts": the game's 256x256 icon sheet has its six pad buttons in the top left
// corner (the four face buttons as 15x15 discs at x 0, 16, 32, 48, then L and R). While on, a
// decoded texture that has those icons there gets them repainted: B, A, Y, X in the places of
// circle, cross, triangle, square, and LB, RB for L, R. Nothing is read from a file. A texture
// already decoded keeps its look until the game loads it again.
void set_xbox_button_icons(bool on);

// The pad buttons' icons from the game's own icon sheet as last decoded: its top left 112 x 16 texels
// (0xAABBGGRR): circle at x 0, cross 16, triangle 32, square 48 (16 wide each), L from 64 and R from 84 (20
// wide each); after the repaint above when that is on. For the interface's own prompts among the game's.
// Returns a number that changes with the picture, 0 while the sheet has not been seen yet (`out` is then left
// alone).
inline constexpr std::uint32_t kButtonIconsWidth = 112u;
inline constexpr std::uint32_t kButtonIconsHeight = 16u;
using ButtonIcons = std::array<std::uint32_t, kButtonIconsWidth * kButtonIconsHeight>;
[[nodiscard]] std::uint64_t button_icons(ButtonIcons &out);

// Key that identifies the decoded contents of a texture for caching.
[[nodiscard]] std::uint64_t texture_key(const GuestMemory &memory, const TextureState &texture);

} // namespace mhp2g::gpu
