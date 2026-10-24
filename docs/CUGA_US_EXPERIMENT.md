# US Windows experiment

**HEAVILY UNTESTED compared with the FUComplete/FUC build.** This separate
branch targets ULUS10391 v1.01. It is an early compatibility experiment,
not full US/EU support. FUC remains on the default branch.

The installer checks the US ISO identity and plain BOOT.BIN SHA256
`3070954e2dc88fd0b0836c0c1dc526531aca4a213b0f18806e324963efb514f4`.
US entry: `0x08906A54`; 32 MiB RAM; 272 PSP imports. Generate the resident
corpus locally from your own executable using the framework recompiler and
the `mhp2g` profile. Generated code and game data must stay outside Git.
Use `-DMHP2G_RELEASE=ON` for distribution; this excludes developer tools
and checkout game-directory fallback. Windows target `Poke` emits Cuga.exe.
Overlay execution in this release uses interpreter fallback; no Japanese
overlay DLLs may be substituted. The unmodified US game needs no FUCDAT.

Limited offline checks: copied-character village/quest entry; analog camera;
controller/keyboard lock-on; Better Shop's combined 79-item stock; map/reveal
off/on; capture skull at a live Giadrome threshold; item-box Triangle restock
with quantity conservation, disabled behavior and menu-close persistence.
Five targeted regression executables passed. All 11 actual US quest lists
were checked privately: 77 key labels, unchanged sizes and other bytes.
No measured code coverage or full-game compatibility claim accompanies this.

Unverified: all quests/monsters/GPUs, other computers, audible output,
save-write/reload round trips, quest-return skip, cloud interoperability,
multiplayer/crossplay and EU executables. Loadouts remain unfinished.
Keep backups and use a copy of any imported save. Do not post game files,
saves, credentials or personal paths in reports. Mention the US release tag.

Based on Project Poke by francl35 and Yakumo by TeamGDB; original license
notices remain. Cuga development used Codex/GPT-6 and earlier Claude Code
assistance, with human playtesting. This branch contains host source only.
