#pragma once

#include "psprecomp/elf32.hpp"
#include "psprecomp/runtime.hpp"

#include <cstdint>
#include <filesystem>
#include <iterator>
#include <array>

namespace mhp2g {

inline constexpr std::uint32_t kLoadBase = psprecomp::kDefaultPspUserLoadBase;
inline constexpr std::uint32_t kGuestRamBytes = 32u * 1024u * 1024u;

struct ProfilePaths {
    std::filesystem::path disc_image;   // UMD ISO; empty disables disc0:
    std::filesystem::path memory_stick; // host directory backing ms0:
};

// Installs the kernel and HLE modules, binds logging stubs for the remaining
// imports (unless MHP2G_STRICT_HLE is set) and prepares the loader thread
// that runs module_start.
void install_profile(psprecomp::Runtime &runtime, const psprecomp::Elf32Image &elf, const ProfilePaths &paths);

// The host directory that backs ms0:, where the game's saves live under
// PSP/SAVEDATA. This is the only place that decides it; the rest of the host
// receives the result, so moving saves to a per-user location changes only
// this function.
[[nodiscard]] std::filesystem::path memory_stick_directory(const std::filesystem::path &game_dir);

// Overlay slots, from the executable's section table. They sit inside the load
// image's reserved BSS, and the game copies code into them at run time, so a
// jump into one stops the runtime until that overlay has its own corpus.
// The end of each slot is the start of the next one.
inline constexpr std::uint32_t kOverlaySlots[] = {
    0x09A5A580u, // *_task mode overlays
    0x09C14280u, // demo_sub, game_sub
    0x09D15100u, // em* monsters
    0x09D5DF80u, // stage* overlays
    0x09D5FF00u, // end of largest known final-slot image
};

} // namespace mhp2g
