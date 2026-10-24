# Cuga developer build notes

This source snapshot is for developers. A complete clean-machine Windows or
Android bootstrap has not yet been validated. Do not treat the inherited
MHP3rd release scripts as a one-command Cuga installer.

## Dependencies

Use a C++20 compiler, CMake, Python 3, SDL3, Vulkan SDK/shader tools, and the
FFmpeg dependencies described in [BUILDING.md](BUILDING.md). Windows development
used Visual Studio 2022 Build Tools and MSVC; run CMake from an x64 developer
shell. Android requires the Android SDK, NDK and Java tools in addition to the
host generation tools. No dependencies or game files are bundled here.

## Configure

For the MHP2G profile, configure from the repository root:

```sh
cmake -S . -B out/mhp2g -DPSPRECOMP_PROFILE=mhp2g -DMHP2G_RENDERER=ON
cmake --build out/mhp2g --target Poke -j2
```

Before local game code is generated, the host uses an empty registry and is
not a playable installation. Build the analysis/recompilation tools and run
`profiles/mhp2g/scripts/generate.sh` with your locally decrypted MHP2G executable
as described in [the earlier MHP2G notes](../README.previous.md). That script
currently assumes a Unix-style shell and `/usr/bin/cmake`; Windows needs the
equivalent locally configured tool invocations.

Generate and compile the required overlay libraries with the MHP2G overlay
tooling. `profiles/mhp2g/scripts/build_overlays.sh` documents its expected
paths; those Unix-style assumptions may need adapting on Windows. Never
commit its analysis output, generated C++ or overlay binaries.

Rebuild `Poke` after generation. CMake still uses the target name `Poke`, while
the resulting program is `Cuga` / `Cuga.exe`. Run it against a local game
directory containing your own prepared game files. Use an isolated save copy
for development; do not test against your only copy of your progress.

## Focused tests

The new QOL and save tests use synthetic data and do not require a game dump:

```sh
cmake --build out/mhp2g --target mhp2g_qol_quest_tests mhp2g_better_shop_tests mhp2g_cloud_save_tests -j2
ctest --test-dir out/mhp2g --output-on-failure -R 'mhp2g_(qol_quest|better_shop|cloud_save)'
```

For all headless framework tests and full test configuration, see
[TESTING.md](TESTING.md). Build only once per build directory and keep
parallelism low; generated units use significant memory. Preserve the CMake
and Ninja dependency state. No runtime header changes are needed for Cuga's
shop, key quest and capture-marker additions.
