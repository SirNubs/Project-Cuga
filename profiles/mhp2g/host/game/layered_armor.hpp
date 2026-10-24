#pragma once

// Layered armor: the hunter drawn in other armor than it wears, part by part,
// while the game keeps the real pieces for defense, skills, the save and what
// it sends to other players. The choices live in the port's settings
// (settings.hpp, look.layered_*), never in the game's memory or save.
//
// Traced in the running game (NPJB-40001); docs/LAYERED_ARMOR.md says how.
// In short:
//
// - The game keeps a 252-byte record per hunter at [0x08AB3640] + 0x30:
//   record 0 is the hunter played here; records 1 to 3 are the other hunters
//   of an ad hoc session (0x08872DC8 fills them, from record 1 on). A record holds the armor
//   ids by part (u16 at +0x1C, +0x26, +0x30, +0x3A, +0x44: chest, arms, waist,
//   legs, head) and at +0x08 a word whose bit 0 is set for a female hunter.
// - Everything traced that draws a hunter in the village, the Guild Hall and
//   on a quest asks 0x08869778 (a0 the block, a1 the record, a2 the part) for a
//   part's model number. That function only reads: the armor table of the
//   part, at the record's id, the male or female model. The file to load,
//   the inner wear of an empty part and the head's special cases all follow
//   from the model it returns.
// - Defense, skills and the Equipment screen read the armor tables by id
//   elsewhere, not through that function; nothing here writes the records,
//   the character or the equipment box, so the save and what the game sends
//   to other players keep the real pieces.
//
// So layered armor replaces that one function while it is on: for record 0
// and a part with a chosen piece it returns the chosen piece's model for the
// record's sex, exactly as the game would for that piece; anything else runs
// the game's own code. It also wraps the hunter's load step so that a changed
// look shows at the game's next load (below). Off from the start, nothing is
// registered and the game runs as built.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace psprecomp {
class Runtime;
}

namespace mhp2g::game {

class Ram;

namespace layered {

// The pointer to the block that holds the hunter records.
inline constexpr std::uint32_t kGameBlockPointer = 0x08AB3640u;
inline constexpr std::uint32_t kHunterRecords = 0x30u;
inline constexpr std::uint32_t kHunterRecordBytes = 252u;
inline constexpr std::uint32_t kHunters = 4u;
inline constexpr std::uint32_t kRecordFlags = 0x08u;  // bit 0: female
// The record of the hunter played on this machine.
inline constexpr std::uint32_t kOwnRecord = 0u;
// The game's model lookup that layered armor stands in for.
inline constexpr std::uint32_t kModelOfPart = 0x08869778u;

// The armor parts in the game's order, which is also the kind byte of their
// pieces: chest, arms, waist, legs, head.
inline constexpr std::uint8_t kParts = 5u;
[[nodiscard]] const char *part_label(std::uint8_t part);

// The choice for each part: kReal (the piece worn), 0 (nothing: the bare part,
// or the inner wear where the game shows it) or an armor piece's id.
inline constexpr std::int32_t kReal = -1;
using Pieces = std::array<std::int32_t, kParts>;

// The model number the game is to draw for `part` of hunter record `index`,
// looked up in block `block` (a0 of the game's lookup), when layered armor
// changes it. Nothing leaves the part to the game: another hunter's record, a
// part left real, a piece this hunter's sex cannot wear, a block or table not
// where they were traced.
[[nodiscard]] std::optional<std::uint16_t> replacement_model(const Ram &ram, std::uint32_t block, std::uint32_t index,
                                                             std::uint32_t part, const Pieces &pieces);

// A piece the choice screen offers.
struct Offer {
    std::uint16_t id{};
    std::string name;
    bool worn{};   // the loaded hunter wears it in that part
    bool owned{};  // worn, or in the equipment box
};
// The pieces of a part the loaded hunter can be shown in: those wearable by
// its sex, owned ones only unless `all`, with the game's names, in id order.
// Empty while no character is loaded.
[[nodiscard]] std::vector<Offer> offers(const Ram &ram, std::uint8_t part, bool all);
// The name of a choice for a part, for the menu: "Real equipment", "Nothing",
// the piece's name from the game, or its id while the names cannot be read.
[[nodiscard]] std::string choice_name(const Ram *ram, std::uint8_t part, std::int32_t choice);

// Showing a change. A hunter loads its part models with a small state machine
// the game steps only while it loads something (an area, a quest, a change of
// equipment at the item box): 0x088A5564, the hunter's vtable +0x94, called by
// 0x088BD1CC until it reports done. +0xB80 is the state (0 start, one step per
// part, 3 and above done), +0xB84 the part it is at, and at each part it asks
// the object's file function (vtable +0xA8, 0x088A58DC for a hunter drawn
// from its record) and loads the file only when it differs from the one it
// has. The game's own request to load them again (0x088A53C8) sets the state
// and the part to 0. So when the look has changed since the hunter played here
// last loaded, the first step the game itself makes on it, with its load done,
// starts it over the same way: the new look shows once the game next loads
// anything, and only the parts that changed are read again.
inline constexpr std::uint32_t kLoadStep = 0x088A5564u;
inline constexpr std::uint32_t kObjectIndex = 0x60u;          // u16, the hunter's record index
inline constexpr std::uint32_t kFileFunctionSlot = 0xA8u;     // in the object's vtable
inline constexpr std::uint32_t kRecordFileFunction = 0x088A58DCu;
inline constexpr std::uint32_t kLoadState = 0xB80u;
inline constexpr std::uint32_t kLoadPart = 0xB84u;
inline constexpr std::uint32_t kLoadDone = 3u;

// Whether `object` is the hunter played here, drawn from record 0.
[[nodiscard]] bool own_hunter(const Ram &ram, std::uint32_t object);
// Whether the hunter's models are loaded (no load running).
[[nodiscard]] bool load_done(const Ram &ram, std::uint32_t object);
// Starts the hunter's load over, as the game's own request does once a load
// is done.
void restart_load(Ram &ram, std::uint32_t object);

// Called at every flip, between two frames: puts the replacement in place
// when the setting is on, and gives the game its own lookup back when it is
// turned off. Off from the start, it never registers anything.
void frame(psprecomp::Runtime &runtime);

// Whether the replacement is in place, for the menu.
[[nodiscard]] bool installed();

} // namespace layered
} // namespace mhp2g::game
