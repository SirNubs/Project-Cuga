#pragma once

// Identity of the one release this profile supports. The hashes match
// config/mhp2g_npjb40001.toml and the profile README.
namespace mhp2g::install {

// DISC_ID in PSP_GAME/PARAM.SFO.
inline constexpr const char *kDiscId = "ULUS10391";
inline constexpr const char *kDiscIdDisplay = "ULUS-10391";
inline constexpr const char *kGameTitle = "Monster Hunter Freedom Unite";

inline constexpr const char *kExecutablePathOnDisc = "PSP_GAME/SYSDIR/BOOT.BIN";
inline constexpr const char *kParamSfoPathOnDisc = "PSP_GAME/PARAM.SFO";

// SHA-256 of PSP_GAME/SYSDIR/BOOT.BIN as it is on the disc.
inline constexpr const char *kEncryptedExecutableSha256 =
    "a62ecebb1bd42a15c556e1f053d3535cc14b0904214c16182cf9762a085f595a";
// SHA-256 of the executable the recompiled code was generated from.
inline constexpr const char *kExecutableSha256 = "3070954e2dc88fd0b0836c0c1dc526531aca4a213b0f18806e324963efb514f4";

} // namespace mhp2g::install
