# Testing

## Automated

- **Kernel and HLE contracts** (`mhp3rd_kernel_hle_tests`) link the actual headless host through `mhp3rd_test_host`. Synthetic guest memory, a tiny public ISO, an independently written square font and temporary save files exercise allocation/coalescing, scheduling, synchronization/deadlines, interrupts/timers, I/O, system calls, utility dialogs, savedata and font dispatch. Real loopback TCP peers check PDP/PTP waits, cancellation, malformed relay records and host/join/stop behavior. User-data paths stay in the temporary fixture; CTest enables bounded OSK diagnostics and applies a 60-second timeout. These checks need no game data, generated game corpus, overlays, graphics or audio device.

- **PNG sizes and allocation failures** (`mhp3rd_png_size_tests`) checks rejection before allocation of invalid dimensions, overflowing filtered images and buffer capacities, padded and negative strides, every PNG filter, grayscale/RGB/RGBA round trips, and cleanup when each writer allocation fails. Tests use small synthetic pixels and a one-MiB allocator limit, with no game data.
- **ImGui texture security and debug formatting** (`mhp3rd_imgui_security_tests`) exercises texture size validation, invalid rectangle/padding rejection, failure before publishing texture state, Alpha8/RGBA pitch and pixel addressing, row-copy guards and default atlas building. Bounded allocator hooks and child processes cover oversized requests and allocation failure in Debug and Release without allocating large buffers. The formatter checks texture IDs above `UINT32_MAX`, ordinary IDs and a truncated output buffer.
- **Image conversion** (`mhp3rd_image_conversion_tests`) checks oversized 16-bit PNG conversion requests before allocation, freeing rejected input, and exact grayscale-to-RGBA output. Its allocator hook keeps synthetic oversized tests below one MiB.
- **Guest PCM staging** (`mhp3rd_guest_pcm_tests`) checks stereo/mono decoding, signed sample extrema, exact-end mapped buffers and rejection of overflowing or unmapped requests before allocation. It needs no audio device or game data.
- **Archive-tool security regressions** run with `python3 profiles/mhp3rd/tests/tool_security_tests.py` and in the desktop CI jobs. Synthetic ISO records and overlay names exercise traversal rejection, existing symlink escapes, malformed directory records and cycles, and overlay command argument handling; they use no game data.
- **Framework tests** run with `ctest --test-dir out/framework` and need no game data.
- **Python lint CI** (`Python lint (Ruff)` in `.github/workflows/tooling.yml`) runs pinned Ruff correctness checks on all first-party Python scripts, tools and tests without game data or native builds. It fails on findings and excludes vendored/generated sources, game directories, overlays and build outputs. The same local command is documented in [CONTRIBUTING.md](../CONTRIBUTING.md#python-tooling); Python formatting is not enforced.
- **Unit-test CI** runs the framework and headless profile tests on standard GitHub-hosted Linux, macOS and Windows runners for pull requests, every pushed branch, and manual dispatch. The workflow in `.github/workflows/tests.yml` builds only `psprecomp_test_binaries` in Debug mode, then runs CTest with failure output and a per-test timeout. Failed jobs upload test logs and JUnit results when available. It uses no game data, generated game corpus, overlays or GPU. FFmpeg is disabled to avoid downloading and building application-only audio/video dependencies. The Vulkan descriptor-pool test is excluded from this renderer-disabled configuration. Android CI also cross-compiles the same targets with NDK 28.2 for `arm64-v8a` and `x86_64` at API 29 (Android 10). The x86_64 binaries run through ADB on a hardware-accelerated Android emulator, including the framework's synthetic code-generation checks. The runner deploys the shared C++ runtime, records each test's exit status and logs, applies a per-test timeout, and uploads diagnostics on failure. ARM64 is a compile check only. APK packaging, Java/JNI integration, Vulkan, gameplay and real Android devices still require separate testing, as does Steam Deck gameplay.
- **CodeQL security analysis** is defined by `.github/workflows/codeql.yml` using advanced setup for Actions, C/C++, Java/Kotlin and Python, with the extended query suite and local as well as default remote input sources on standard GitHub-hosted runners. C/C++ uses no-build extraction, so it needs no game data, generated corpus or overlay rebuild. Java uses a manual `javac --release 11` compilation of Yakumo's Android wrapper with Android API 35 and the release-pinned SDL3 dependency. SDL Java classes are prepared before CodeQL initialization; the traced app compilation resolves them through its classpath. No native code, emulator, APK, game data or overlays are needed for Java analysis. Review results under *Security and quality → Code scanning*; a successful scan does not replace builds or gameplay tests. GitHub Code Quality is a separate product and remains disabled.
- **Full application builds on every platform** — further work tracked in [#15](https://github.com/TeamGDB/Yakumo/issues/15). Unit-test CI does not build playable releases; release packaging remains a separate process.
- **Regression tests on your own copy of the game**, replaying recorded input and comparing frames against reference images — planned in [#16](https://github.com/TeamGDB/Yakumo/issues/16).

Unit tests do not replace gameplay checks. Use the smoke test below for changes that affect the game.

To reproduce the headless CI configuration from a clean checkout without game data:

```sh
cmake -S . -B out/ci -DCMAKE_BUILD_TYPE=Debug -DPSPRECOMP_PROFILE=mhp3rd -DPSPRECOMP_BUILD_TESTS=ON -DMHP3RD_RENDERER=OFF -DMHP3RD_FFMPEG=OFF -DPSPRECOMP_MSVC_MP_JOBS=1
cmake --build out/ci --config Debug --target psprecomp_test_binaries -j2
ctest --test-dir out/ci -C Debug --output-on-failure --no-tests=error --timeout 120
```

For suspected vulnerabilities, follow [SECURITY.md](../SECURITY.md) and report privately. Dependabot alerts and security updates cover supported dependency manifests; libraries downloaded by CMake or vendored in the profile still need separate version and advisory checks. Secret scanning and push protection are enabled for this public repository.

### Native C++ coverage

`make coverage` uses a separate instrumented Debug build in `out/coverage`,
compiles public native tests, the application with an empty generated-code
registry and the command-line tools, runs the full registered suite and merges
fresh Clang profiles. It never compiles local game-derived generated units or
rebuilds overlays. Normal builds keep `PSPRECOMP_COVERAGE=OFF`; coverage and
sanitizers use separate build directories.

The project coverage workflow runs on every branch and PR. It enables the
Vulkan renderer and system FFmpeg, builds the pinned SDL dependency, and
collects native, Python and Android Java reports. It runs the complete suites
before requiring **80% coverage of changed executable lines**; the overall
percentage has no minimum. The `project-coverage` artifact includes per-language
HTML reports, native LCOV, the JSON/Markdown source inventory and diagnostics.
See [COVERAGE.md](COVERAGE.md) for commands, comparison bases and remaining gaps.

On Linux use Clang/LLVM 18 and compiler-rt; on macOS use Xcode Clang and its
matching `xcrun llvm-cov`/`llvm-profdata`. `COVERAGE_CXX` selects the compiler,
but report tools must match its profile format. Native coverage currently
supports neither Windows nor cross-compilation. Headless native tests still
run on all supported desktop runners and on the Android x86_64 emulator.

The native denominator includes tracked first-party production C/C++ under
`include/psprecomp/`, `src/`, `tools/` and `profiles/mhp3rd/host/` that have a
coverage mapping in the application, tools or test binaries. Vendor sources,
test code, generated/game code and Python are excluded. Unmapped sources stay
visible with unknown executable-line counts; absent platform paths and child
processes that abort without flushing profiles remain gaps. Compiling renderer
and audio mappings does not establish GPU/audio behavior coverage. The report
does not claim the entire project has been tested.

The runner clears previous native reports/profiles before testing. Missing
instrumentation, test failures and collection failures fail the job. LLVM can
report unused zero-hash inline stubs when another translation unit emits the
function; only stubs whose emitted mapping exists are accepted, with retained
diagnostics. Nonzero mismatches and missing emitted mappings fail reporting.
Collection uses one LLVM export/rendering worker: LLVM 18's parallel debug
export has aborted with an invalid free in CI. The serial mode retains the
same binaries and mappings and still fails on unreviewed diagnostics.
See [Clang's coverage guide](https://clang.llvm.org/docs/SourceBasedCodeCoverage.html)
for the source-based metrics.

### Native ASan and UBSan

The separate `.github/workflows/sanitizers.yml` job uses Ubuntu 24.04 and the versioned Clang 18 toolchain packages (including compiler-rt and llvm-symbolizer). Distribution security updates remain available; the job prints the effective compiler version. Normal builds keep `PSPRECOMP_SANITIZERS=OFF`. The opt-in configuration instruments compilation and linking of the framework, profile test sources and dependency implementations with `-fsanitize=address,undefined`, debug information, frame pointers and `-O1`. UBSan recovery is disabled; ASan/UBSan reports and detected leaks fail the check. Sanitizer failures use direct nonzero exits instead of SIGABRT, so the existing negative tests cannot mistake a report for an expected assertion abort. No suppressions or test exclusions are configured.

Reproduce it on native Linux with Clang 18 installed:

```sh
cmake -S . -B out/sanitizers -G Ninja -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Debug -DPSPRECOMP_SANITIZERS=ON -DPSPRECOMP_PROFILE=mhp3rd -DPSPRECOMP_BUILD_TESTS=ON -DMHP3RD_RENDERER=OFF -DMHP3RD_FFMPEG=OFF
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=0:exitcode=99
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1:abort_on_error=0:exitcode=98
export ASAN_SYMBOLIZER_PATH=/usr/bin/llvm-symbolizer-18
python3 scripts/ci/check_sanitizers.py out/sanitizers
cmake --build out/sanitizers --target psprecomp_test_binaries -j2
ctest --test-dir out/sanitizers --output-on-failure --no-tests=error --timeout 120
```

The instrumentation check creates and removes a temporary CMake project using the same sanitizer module and configured compiler. Heap use-after-free, signed overflow and a leaked allocation must each produce their specific sanitizer diagnostic and a failing exit status. The complete headless native suite includes parser, allocation, PNG, both font libraries and ImGui boundary regressions using public synthetic inputs. Failures retain CTest, configure and probe logs as CI artifacts for seven days.

ASan typically adds about 2x execution time according to [the Clang documentation](https://releases.llvm.org/18.1.8/tools/clang/docs/AddressSanitizer.html); memory and build overhead vary by test. This configuration targets native Linux Clang 18 or newer only. Windows, macOS, Android, Vulkan, audio/FFmpeg, generated guest code, overlay libraries and full gameplay are not validated by this job. Sanitizers observe executed paths and do not prove unexecuted code safe; see [UBSan's documented checks and recovery policy](https://releases.llvm.org/18.1.8/tools/clang/docs/UndefinedBehaviorSanitizer.html).

### Switching CodeQL from default to advanced setup

GitHub default setup blocks result uploads from advanced workflows. When deploying the committed CodeQL workflow, disable default setup under the repository's security settings, then dispatch the `CodeQL` workflow on `main`. Confirm that all four language analyses complete and Java resolves the Android/SDL calls before retiring the previous default configuration. Keep default setup active while reviewing this migration; local Java compilation alone does not verify CodeQL database quality or SARIF uploads. The language categories remain `/language:actions`, `/language:c-cpp`, `/language:java-kotlin` and `/language:python`; existing alert history must be checked after the first advanced scan.

To reproduce only the Java compilation with a local Android SDK and the release-pinned SDL source:

```sh
python3 scripts/ci/android_java.py "$ANDROID_HOME/platforms/android-35/android.jar" /path/to/SDL3-source out/java-check --stage sdl
python3 scripts/ci/android_java.py "$ANDROID_HOME/platforms/android-35/android.jar" /path/to/SDL3-source out/java-check --stage app
```

These checks compile SDL first, then Yakumo. In the CodeQL workflow initialization runs between those stages so the app is analyzed with its actual dependencies. Android API 35 is the compilation API, not a change to the application's Android 10 minimum requirement.

### Font bitmap allocation guards

`mhp3rd_font_bitmap_tests` and `mhp3rd_imgui_font_bitmap_tests` exercise both vendored stb_truetype copies with the same independently authored synthetic square font. They check oversized and invalid bitmap/atlas dimensions, row strides, non-finite scales, SDF padding and allocation failures, oversized or failed rasterizer scratch allocations, a glyph too wide for the baking atlas, and unchanged small bitmap/SDF output. The allocator and clear hooks bound synthetic requests; no external font, game data or GPU is required. These checks do not establish that stb_truetype can safely parse arbitrary untrusted fonts.

## Smoke test

About fifteen minutes. It walks through every part of the game that currently works, so a regression anywhere shows up. Start from a fresh profile — rename `profiles/mhp3rd/game/ms0` aside — so earlier state cannot hide a problem.

Before you start, write down the commit you are testing: `git rev-parse --short HEAD`. A result is only useful with it.

A released build is tested the same way. Note its version and which download it is (Flatpak or tarball) instead of the commit, start it through its launcher (`flatpak run io.github.teamgdb.Yakumo` or `./yakumo`, from a terminal to see the console), and start from a fresh data directory: for the Flatpak, move `~/.var/app/io.github.teamgdb.Yakumo` aside; for the tarball, `~/.local/share/Yakumo`. The first start then runs the setup from your disc image, which is part of the test. [`LINUX.md`](LINUX.md) says where a release keeps its saves.

| # | Step | Expected |
| --- | --- | --- |
| 1 | Start `out/mhp3rd/bin/Yakumo` | A window opens; the console lists 355 overlay corpora, the renderer and the audio device |
| 2 | Wait through the logos | Movies are skipped (see #6) and the title screen appears; streamed music is silent (see #5) |
| 3 | Start a new game | Character creation appears |
| 4 | In character creation, change each option | The character model is whole and textured, animates, and changes with each option |
| 5 | Enter a name, confirm, and save to a slot when asked | The console logs `[savedata] saved … ULJM05800 (encrypted)` and the game moves on to the hot spring scene |
| 6 | Watch the hot spring scene | Water, steam and the waterfall draw correctly; characters have soft shadows, not white patches |
| 7 | Talk through the scene and walk out | The village loads; the marker over an NPC's head is red, characters are shaded, and distant geometry fades into the fog |
| 8 | Walk around the village | Everything draws; it runs slower than elsewhere (see #7) |
| 9 | Take a quest and depart | The quest map loads with the HUD, the minimap and the character's weapon |
| 10 | Hunt a small monster | Monsters appear and animate; attacks, hits and sound effects work |
| 11 | Stand still with no input for ten seconds | The character and the camera stay still |
| 12 | Move the camera with the right stick, if you have a gamepad, and with the mouse | The camera turns and stops when the stick is released or the mouse stops |
| 13 | Return to the village | The village loads again |
| 14 | Close the window and start the game again | The console logs `[savedata] loaded … (decrypted)`; after the title screen, character select lists the character from step 5 |
| 15 | Pick that character | The game continues from the save |

### What to watch for throughout

- Any line reading `[interpreter] no recompiled function at …` — that code runs about twenty times slower. Note the address.
- Missing, torn or flickering geometry, and black or white patches where effects should be.
- The console's last lines if the game stops or crashes.

### Analog camera regression (#106)

Build `mhp3rd_camera_tests` and run it through CTest. These checks require no game data and cover dispatch interception, proportional rates, fractional yaw, pitch limits, release without filter catch-up, Off passthrough, special modes, scene changes and the extent of guest writes.

For the manual check, start without `MHP3RD_TRACE_CAMERA` or `MHP3RD_FIND_CAMERA`. Keep **Controls → Analog camera** on (the default), then enter an ordinary quest. Test small and full stick deflections on both axes, release, reversal, movement near walls, a zone transition, L recentre, physical D-pad commands, and Off/On toggles. With a bow and a bowgun, aim (R, and the bowgun scope) and move the right stick, then the mouse: the aim must move in proportion on both axes without shaking when the motion starts, stops or reverses, stop at its vertical limits, and the camera follow it; the analog camera must take over again after the aim. Confirm that the village cameras retain their own behaviour. Watch for residual vertical coast and camera movement after input has stopped. Repeat on Steam Deck before marking that platform verified.

### Picture shape and size (#117)

Build `mhp3rd_aspect_tests` and run it through CTest. These checks need no game data and cover what is written to guest memory for a wider or narrower view, that the game's own shape writes nothing, that switching back restores every value bit for bit, and that different game code is left alone.

For the manual check, open **Video** in the menu and compare the three **Aspect ratio** values on the same screen, in a quest and in the village:

- **Original** with a fixed resolution looks exactly as before, bars included.
- **Fill** fills the window with no bars. Circles (the minimap, round icons) stay round; the 3D view is not stretched: a monster turning in place keeps its proportions. The HUD sits in a centred PSP-shaped area.
- In **Fill**, look for objects popping in or out at the left and right edges while the camera turns, especially at 21:9 or wider.
- Fades, the pause menu's darkening, the blur of the item and map menus, and the quest-reward screen cover the whole window.
- Switching the value back and forth takes effect at once; the console logs `[aspect] the game's view is … wide to 1 high`, and back at Original the game's `1.76471`.
- With **Resolution** on Auto, resize the window, toggle fullscreen and, where possible, move it to another display: after a moment the console logs `[render] internal resolution W×H` with the window's size.
- On a Steam Deck (1280×800, 16:10), Fill with Auto draws 1280×800 and should hold 30 fps in a quest.
- On a phone with a notch or a camera hole (#170), in both landscapes, after turning the phone, after returning from the home screen and after pulling down the notification shade: Fill covers the whole screen, the cutout's edge included, with no black strip beside the cutout; Original shows bars of the same width on both sides; Stretch is letterboxed evenly by the cutout's depth. The touch controls stay clear of the cutout. The log's `[render] layout` lines give the window, the surface, the cutout and the picture's place for a report.

### Keyboard and mouse (#94)

`mhp3rd_input_tests` (CTest) checks the bindings and control presets without SDL or game data: key and pad button names, how `settings.ini` spells them and their combinations, the shipped presets, turning earlier versions' bindings and trigger profiles into a preset, the menu's rebinding rules, conflicts and what held inputs press. `mhp3rd_settings_tests` reads and writes presets through `settings.ini`'s keys. `mhp3rd_camera_tests` covers the mouse in the camera layer: its turn in the ordinary camera, sizing the game's aim steps from the mouse (including a step the game makes an update late, and one made after the mouse stopped), and switching the game's own turn where the port does not drive the camera.

For the manual check, unplug the gamepad (or leave it untouched) and play from the title screen with the keyboard and mouse only, on the default layout (see the profile README's *Keyboard and mouse*):

- While the game window has focus the pointer is hidden and captured. Esc opens the menu and the pointer comes back; closing the menu takes it again. Switching to another window (Cmd+Tab, Alt+Tab) gives it back; returning takes it again. The on-screen keyboard for the hunter's name and the setup screens never take it.
- Create or load a hunter, walk the village with W A S D, talk (F or the right button), take a quest from the menus (F or the right button confirms, Space goes back) and depart.
- In the quest, the mouse turns and tilts the camera smoothly and stops where it stops; *Mouse sensitivity* and both *Invert mouse* settings change it at once. With *Analog camera* off, moving the mouse sideways turns the game's own camera while it moves. Q puts the camera behind the hunter.
- Attack with the left button, roll with Space, use an item with E, guard or run with Left Shift. With a bow: hold Left Shift and move the mouse, the aim follows in proportion, then shoot with the left button; with a bowgun, fire with the right button. Rolling or walking while aiming must not move the aim more than the game allows.
- Hold a key or a mouse button, open the menu with Esc, release it, close the menu: nothing stays pressed. Pick the gamepad up mid-quest and put it down again: both work, and no camera motion is left over.
- In Controls, rebind a control (activate its row, press a key or a mouse button), check it in play and in `settings.ini`: the change becomes a preset of your own named *Custom*. Bind a combination (hold Left Shift, press F) to *△ + ○ (together)* and check the combined attack in a quest. Bind a key another control has and see both rows turn red. Switch *Preset* to Classic keyboard and back, rename and delete your preset, and try *Restore control defaults*.
- With a gamepad, rebind a button under *Gamepad buttons* and a combination such as LB + ○; try the Modern preset (RT attacks, LT guards) and Left-handed (the right stick moves).

With `MHP3RD_TRACE_PAD=1` the console shows `[pad] pointer captured` and `[pad] pointer free` as the pointer changes hands, and the mouse's motion in counts and degrees.

### Touch controls (#174)

`mhp3rd_touch_tests` (CTest) checks both touch layouts without a screen: that the Action layout fits 20:9, 16:9 and tablet shapes with no two elements overlapping, how `settings.ini` spells a placement, that a button presses its PSP buttons (the combined attack △ and ○ in one frame), that the stick, a button and a camera drag work at once, that a thumb slides across the attack cluster, that a quick swipe presses D-pad Left or Right for a moment and a slow drag in the swipe area turns the camera instead, and that hidden and rebound elements behave. On a desktop the input script's `finger`, `hold` and `swipe` steps are a virtual touch screen, and `drag` moves the menu's pointer as a touch does (see `host/ui/input_script.hpp`).

On a phone, with Controls → Touch screen → *Layout* set to Action, in a quest:

- Move with the stick while attacking and turning the camera with another finger; hold R (Guard) and attack; tap the combined attack button and see the combined attack come out at once.
- Evade, sheathe with □, open the item pouch with L and scroll the item bar with quick swipes left and right in the area above the middle; a slow drag there turns the camera.
- Pause opens Yakumo's menu; Start and Select open the game's.
- *Edit the action layout…*: move, resize, rebind (for example Special to R + △) and hide elements, change the opacity and haptic feedback, leave and come back, restart the app: the layout is as you left it. *Reset the whole layout* brings the first one back.
- Rotate the phone and come back from the background: the layout stays clear of the cutout and the game's health, sharpness and item bar.
- Set *Layout* back to PSP buttons: the old layout is unchanged.

### Texture pack import (#49)

`mhp3rd_texture_pack_tests` (CTest) checks the import without game data or a window: finding a pack in each layout (the pack folder, `textures/NPJB40001`, `NPJB40001`, `PSP/TEXTURES/NPJB40001`, in any case), a pack named for `ULJM05800` taken only when its `[games]` lists `NPJB40001`, `quick` and hashless packs refused, zipped packs refused with "unpack it first", key, image, size and missing-image counts, the copy into a staging folder, the swap that moves the old pack to `textures/.backup/<date>_<time>/NPJB40001`, cancelling, and where the pack is read from with `MHP3RD_TEXTURE_PACK` and a pack used in place. `mhp3rd_texture_pack_tests --check <folder>` prints what the menu would find in a real folder and reads nothing else.

For the manual check, use a throwaway data folder (`MHP3RD_DATA_DIR`, with a copy of `settings.ini`, `EBOOT.ELF` and the disc image) and a real pack, then in **Video**:

- *Import texture pack…* with the gamepad only: browse to the pack, open its folder (it is chosen at once), and read the review: the key count, hash, images, size, missing images, free space and the pack in use now. Back returns to the folders; *Cancel* imports nothing.
- Choose the pack's parent folder, a folder holding `PSP/TEXTURES/NPJB40001`, and a copy renamed `ULJM05800` with and without `NPJB40001 = true` under `[games]`: the first three are found, the last is refused with its reason. A copy whose `textures.ini` says `hash = quick` is refused.
- *Copy into Yakumo's data folder*: the progress bar moves, the game keeps drawing (or stays paused) without stutter, Esc/Start do not close the menu. Cancel half way: the console logs `[texpack] copy cancelled`, the result says nothing changed, `textures/` holds no `.incomplete-…` folder and the old pack still draws.
- Copy again to the end: the Texture pack row shows the new count within a frame or two and the textures change on screen. Import a second time: the first pack is in `textures/.backup/<date>_<time>/NPJB40001`, whole.
- *Use it where it is*: nothing is copied, `settings.ini` has `video.texture_pack_folder`, the textures stay. Rename the pack folder and turn *Texture pack* off and on: the row says *Pack folder missing: …*. *Stop using the pack folder* goes back to the installed pack.
- With the keyboard and mouse: the same with clicks, and drag the pack folder from the file manager onto the window while the browser is open.

### Mods (#79, #80, #82)

`mhp3rd_mods_tests` (CTest) needs no game data: `mod.ini` reading (quoted values, lists, a quote left open, `Version` and the PSP default, `FilesHD`/`TargetHD`, packs, pseudo packs, equipment slots, code mods refused, missing files and bad targets), mhp3reload's files named by id, priority and conflicts, packs and dependencies, the saved choices, import and its backup, the `DATA.BIN` obfuscation from any byte, and a small archive served with a grown replacement, a smaller one, a patch and a moved verbatim entry, read whole and in pieces. `mhp3rd_mods_tests --check-disc <image.iso>` reads the real directory (only that) and checks that it encodes back to the disc's bytes.

For the manual check, never use downloaded mods for a regression you cannot undo: use a throwaway data folder (`MHP3RD_DATA_DIR`, with copies of `settings.ini`, the saves, `EBOOT.ELF` and the disc image) and `MHP3RD_MODS_DIR` pointing at a scratch folder, and make test mods from the game's own files with `profiles/mhp3rd/tools/databin.py … extract`: for example file `0FEE` (the game menu's textures) with part of each texture's pixels overwritten, in a folder with a `mod.ini` of `Type="File"`, `Version="HD"`, `Files`, `Target="0FEE"`. Run with `MHP3RD_TRACE_MODS=1`.

- With no mods folder, the log has no `[mods]` lines beyond the count, and the game is unchanged.
- Turn the mod on in **Mods** and restart: the log lists `0FEE <- …` and `[mods] read 0FEE …` lines, and the game menu (after the title) shows the change. Turn it off: *Applied* and, after a restart, the original textures. `MHP3RD_NO_MODS=1` gives the original too, with the mod still on in the menu.
- A copy of the same file 300 KiB larger (zeros appended): *Restart to apply* and *Restart now*; after the restart the log says `DATA.BIN grows from 1208858624 to …` and the title screen, its music and the intro movie, all read from entries after the grown one, are as before.
- A patch mod (`Type="Patch"`) for `0FEF` with blocks of `(offset, length, bytes)` into its textures and `FFFFFFFF00000000` at the end: stripes on the title screen. Together with the grown `0FEE`, which moves `0FEF`, the same.
- Two mods on the same file: *Conflicts* names the winner; *Priority* left and right changes it (after the next load or a restart).
- A `mod.ini` without `Version`, a `Type="Code"` mod and one with a missing file show as *Cannot be used* with the reason.
- *Import mod…* with the gamepad only, then with the mouse and by dropping a folder on the window: a single mod folder, a folder holding two, and a mod already installed (it moves to `mods/.backup/<name>-<time>`). The imported mods are off. A preview.png shows on the mod's screen.
- An equipment mod: type the target file id on the on-screen keyboard; the row shows it and `mods.ini` has `slot1=`.

### Folder names outside ASCII (#129)

`mhp3rd_path_tests` (CTest) needs no game data. It works in a temporary folder named `Юникод_テスト` and checks the UTF-8 conversions and environment variables, `settings.ini` with UTF-8 paths in it, writing a save there (and that no `.tmp` file is left), exporting and importing it, finding, checking, copying and installing a texture pack whose image has a Cyrillic name, opening a font copied there (skipped when the system has none of the fonts it looks for), opening a disc image and an executable there, the data folder named by `MHP3RD_DATA_DIR`, a portable `data` folder beside a `portable.txt`, and writing a screenshot into the data folder.

The manual check matters most on Windows, where the standard library's narrow paths use the ANSI code page. Use a user folder, or a data folder, whose name that code page cannot hold: Cyrillic on a Western system, Japanese on a Russian one.

- Either sign in as a user with such a name (for example `Юзер`), or set `MHP3RD_DATA_DIR` to a folder like `C:\Игры\ヤクモ データ` in the terminal you start Yakumo from (`$env:MHP3RD_DATA_DIR = 'C:\Игры\ヤクモ データ'` in PowerShell).
- Put the disc image in a folder like `C:\Образы\モンハン` and run the setup (`Yakumo --install`, or delete `settings.ini` to get the screens): the file browser shows the folder names correctly, the checks pass, and both *Copy* and *Use in place* finish. `settings.ini` in the data folder, opened in Notepad as UTF-8, shows `disc_image=` with the full name.
- Also from the terminal: `Yakumo --install "C:\Образы\モンハン\MHP3rd HD.iso" --in-place`, then `Yakumo` alone starts the game.
- Save in game, quit, start again: the save loads. The console's `[savedata] saved … bytes to …` line names the folder (as `?` where the console font has no glyph, which is fine) and the game does not stop there.
- In the menu's *Saves*: *Export save…* to a folder with such a name, *Import save…* back from it (the check passes, *Restart now* restarts Yakumo and the save loads), *Back up saves…* to such a folder; *Open the data folder* and *Open the backups folder* open Explorer in the right place.
- *Import texture pack…* from a folder with such a name, both copied and *Use it where it is*; restart and check the pack still loads.
- Put a `.ttf` with a Cyrillic file name into the data folder's `fonts` folder (*Open the fonts folder* under **Video → Font**) and pick it under *Font*: it is listed and used, and still used after a restart.
- *Set up game data again…* in the menu restarts Yakumo into the setup.
- *Save network log* under **Network** writes a file into `logs` in the data folder.
- `MHP3RD_SCREENSHOT_DIR` set to a folder with such a name gets the screenshots.
- F12 in the game writes a screenshot into `screenshots` in the data folder, and *Open the screenshots folder* opens it.
- A portable copy: Yakumo unpacked into a folder with such a name, with `portable.txt` beside `Yakumo.exe`, keeps settings and saves in its `data` folder.
- **Controls → Controllers**: set up a controller; `gamecontrollerdb.txt` is written into the data folder and read again at the next start (the log's `[pad] … mapping(s) … from` line).

### Renderer performance paths (#92)

Build `mhp3rd_render_tests` and run it through CTest: it checks, without a GPU or game data, that the index lists transformed draws are now drawn with name exactly the vertices the old expansion wrote, in the same order.

The speed changes each have an off switch that restores the old path: `MHP3RD_NO_DIRECT_VERTICES`, `MHP3RD_NO_LOOKUP_CACHE`, `MHP3RD_NO_BUFFER_REUSE` and `MHP3RD_NO_DRAW_MERGE`. When a frame looks wrong, run once with all four set: if the fault goes away, set them one at a time to find the change behind it, and report which. `MHP3RD_CHECK_DIRECT_VERTICES=1` compares every transformed draw with the old expansion while playing and prints `[direct-check] N draws compared, M differed`; M must stay 0.

To measure, run with `MHP3RD_PERF=log MHP3RD_TRACE_STALLS=1` and stand still in the village by the shop and the smithy passage for a minute. `MHP3RD_PERF_ALTERNATE=direct,lookup,reuse,merge` turns the new paths off every other second; compare the `render` and `gpu` numbers of the `alt on` and `alt off` lines. A spike shows up as a `[slow-frame]` line naming where the frame waited.

### Frame rate (#39)

Build `mhp3rd_interpolation_tests` and run it through CTest. It needs no GPU or game data and checks matching draws between two frames, the cut rules (a camera turn that keeps growing past 30 degrees blends, a sudden one does not), blending, when each present falls and what it shows at 45, 60, 90 and 120 (the blend factors, no picture going back, skipped presents never queued), and how the frame rate steps down and back up.

For the manual check, open **Video → Frame rate**:

- **30** looks and times exactly as before. The `[perf]` line has no `interpolation` field.
- At **60**, walk and turn in the village and in a quest: movement, the camera and characters' limbs are smooth, with no doubled or jumping objects. The interface and the minimap stay as they are. A camera cut in a cutscene, entering an area and a loading screen show no blended frame.
- In a quest with **Controls → Analog camera** on and *Camera speed* at 720, turn the camera at full deflection, alone and diagonally with the tilt: no stutter, and with `MHP3RD_TRACE_INTERPOLATION=1` the `[interp]` lines show no `camera turned` or `camera moved` cuts while turning (a turn measured 24.5-25.4 degrees and 174-180 units per game frame on the Mac).
- Switching the value while playing takes effect at once; the menu pausing the game and resuming it, changing *Resolution*, *Aspect ratio* or *Vsync*, and turning *Game speed* to Unlimited (which greys the row) do not break it. Switching to 120 on a 60 or 90 Hz screen with Vsync never stalls the game: the row shows *120 (running at 90)*.
- **Lower when behind**: with it off, the chosen rate stays even if `speed` drops; with it on (the default) a rate the machine cannot hold steps down within two seconds and the log says why. *Restore video defaults* sets it back to On.
- The pad: walking, attacking and turning respond as quickly at 30 as before, or quicker. With `MHP3RD_PAD_AT_FLIP=1` the old reading at the flip comes back, to compare.
- During a camera turn the `[interp] plain:` line must show only `at the newest frame` (30 a second), and no `camera moved` cuts; the hunter must not shake against the scenery.
- The braziers in front of the guild hall and in the gathering hall burn as fast as at 30. NPCs, villagers and single objects never appear displaced for one present while you walk or turn; the `[interp] guards:` line counts the pairs it gave up. For comparison, `MHP3RD_INTERPOLATION_NO_FLIPBOOK_GUARD=1` and `MHP3RD_INTERPOLATION_NO_MOTION_GUARD=1` bring the old behaviour back.
- `MHP3RD_CHECK_REPLAY=1` must print `0 of N pixels differ` for every check.

To measure, run with `MHP3RD_PERF=log MHP3RD_TRACE_INTERPOLATION=1` and stand still in the village by the shop and the smithy passage. `MHP3RD_FRAME_RATE_CYCLE=30,45,60,90` switches the rate every ten seconds; skip the first two `[perf]` lines after each `[interp] cycle:` line. For each rate read:

- `[perf]`: `fps` (45, 60 or 90 — on a Steam Deck with Vsync, 120 and *Match display* run at 90), `speed` (100%), `render` and `gpu` per game frame, and `frame … max` (no regular spikes above the present interval).
- `[interp]`: `presents` and `skipped` (0 or close to it), the ms of a blended present and of its `recording`, its `gpu`, `late up to` and the `delay` (at 90 on a Steam Deck about 26 ms, with the game's `code` about 4 ms). On the `plain:` line only `at the newest frame` should count, and `display busy` and `over budget` should stay at 0.
- No `[interp] frame rate … -> …` line while standing still; if one appears, it gives the speed, the spare time a frame and the cost of a blended present that made the rate step down.

`MHP3RD_INTERPOLATION_EXTRA_MS=16` with *Frame rate* 90 shows the step-down on a fast machine: within a few seconds `[interp] frame rate 90 -> 60: the game had no time to spare`, then 60 fps with nothing skipped.

### Save data (#167)

`mhp3rd_savedata_tests` (CTest) needs no game data: encryption and `PARAM.SFO` round trips, checking a save, import with the replaced save moved to `.backup`, export, backups with and without a timestamp (an earlier backup is replaced only when the player agrees), and the release a build's version belongs to, which decides when the backup reminder shows.

For the manual check use a throwaway data folder (`MHP3RD_DATA_DIR`, with a copy of `settings.ini` and `EBOOT.ELF`) and only copies of saves. Never run it against your own saves folder. Hash the save files (`md5`, `sha256sum` or `certutil -hashfile`) before and after each step:

- **Create and save.** From an empty `ms0`, start a new game, make a character and save it to slot 1: `[savedata] saved … (encrypted)`, and `ms0/PSP/SAVEDATA/ULJM05800` holds `PARAM.SFO`, `MHP3RD.BIN`, `ICON0.PNG` and `PIC1.PNG`.
- **Load after a restart.** Close and start again: `[savedata] loaded … (decrypted)`, and *Continue* lists the character.
- **Overwrite.** *New game*, make another character and pick the used slot. *Character already exists. Delete previously saved character?* answered *No* leaves `MHP3RD.BIN` unchanged; *Yes* saves the new character over it.
- **The reminder.** With a save present, set `saves.backup_reminded=` to an older release (or empty) in `settings.ini` and start without `MHP3RD_INPUT_SCRIPT`: the game pauses on *Back up your saves*. *Back up now* makes `save-backups/<date>_<time>/ULJM05800` identical to the save, and the next screen shows where, with *Open the folder* and *Continue*. The next start with the same build does not ask; `saves.backup_reminder=0` or `MHP3RD_BACKUP_REMINDER=0` never asks; `MHP3RD_BACKUP_REMINDER=1` asks every time, and lets a scripted run drive it.
- **Back up saves….** In *System*, a timed backup makes a new folder each time. With the timestamp off, the second backup to the same folder asks *Replace the earlier backup?*: *Cancel* keeps it, *Replace the backup* replaces it.
- **Import.** Import a copy of a PSP save (the 100% test save, `ULJM05800/MHP3RD.BIN` encrypted), by browsing or by dropping its folder on the window: the review shows both saves, asks for a backup before anything is written, and *Back up now* reports the folder. *Replace and import* moves the old save to `ms0/PSP/SAVEDATA/.backup/<date>_<time>/`, the imported `MHP3RD.BIN` is byte for byte the source, and *Restart now* reaches character select with the imported characters.

## Reporting

Open a **Test report** issue with the platform, hardware, commit and the steps you reached. If a result changes a cell in [the compatibility table](COMPATIBILITY.md), update the table in a pull request as well.

Useful settings while testing — all described in [the profile README](../profiles/mhp3rd/README.md#configuration):

- `MHP3RD_SCREENSHOT_DIR` and `MHP3RD_SCREENSHOT_EVERY` capture frames to attach to a report.
- `MHP3RD_TRACE_PAD=1` shows whether input is reaching the game.
- `MHP3RD_TRACE_AUDIO=1` shows audio levels and dropped frames.
- `PSPRECOMP_HLE_HISTOGRAM=1` prints which system calls the game made.

## Focused clang-tidy analysis

Stage 3 (#252) uses LLVM clang-tidy **22.1.8**, with hash-checked standalone
wheels for Linux x86_64 and macOS arm64. The explicit checks in `.clang-tidy`
cover suspicious `sizeof`/`memset`, implicit pointer-to-bool conversion,
dangling handles, null string views and use after move. Selected findings and
compilation errors fail the check; style and modernization rules are excluded.
See the [LLVM 22.1.8 documentation](https://clang.llvm.org/extra/clang-tidy/index.html).

From a clean public checkout without generated game code:

```sh
python3 -m venv out/tidy-tools
out/tidy-tools/bin/pip install --require-hashes -r scripts/ci/clang-tidy-requirements.txt
cmake -S . -B out/tidy -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DPSPRECOMP_PROFILE=mhp3rd -DPSPRECOMP_BUILD_TESTS=ON \
  -DMHP3RD_RENDERER=OFF -DMHP3RD_FFMPEG=OFF
python3 scripts/ci/clang_tidy.py
```

The runner selects first-party `src/`, framework tests, profile host and profile
C++ test translation units present in the real compilation database. It does
not invent include paths or feature definitions. Required vendor headers are
parsed; vendor/generated translation units and their header diagnostics are
excluded from first-party enforcement. Each source's target configurations
remain in the database. First-party header findings are reported when included
by an analyzed translation unit. No AOT, overlay or application build is needed.

The Linux hosted job has a 25-minute deadline and each translation unit has a
120-second limit. It uploads per-source logs, elapsed time and a coverage
manifest even on failures. This configuration does not cover renderer-enabled
SDL/Vulkan code, FFmpeg, Windows/macOS-only branches, Android JNI, shaders,
or game-generated code. macOS local runs cover the available native branches;
they are not a substitute for the Linux job or existing platform builds.

When using Apple's compiler with standalone LLVM on macOS, make its implicit
SDK and libc++ directories explicit in the CMake database before analysis:

```sh
cmake -S . -B out/tidy \
  -DCMAKE_OSX_SYSROOT="$(xcrun --show-sdk-path)" \
  -DCMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES="$(xcrun --show-sdk-path)/usr/include/c++/v1"
```

The pinned LLVM 22 frontend supports the current Apple SDK headers; older
standalone LLVM 18 could not parse their newer builtin type traits. Do not
remove those headers or suppress parser failures to make analysis pass.

The runner prepares public headers with the existing CMake generators directly.
It runs only version and NID-table generation from
tracked metadata; it does not compile the game stub, AOT or overlays.

## Complementary Cppcheck analysis

Stage 4 (#253) pins **Cppcheck 2.22.0** by release source SHA-256. Its independent
parser/dataflow analysis complements clang-tidy's focused AST checks with array
bounds, uninitialized variables, null dereferences and resource lifetime
errors. The initial policy blocks first-party `error` diagnostics; `warning`
diagnostics are uploaded for review without blocking. Style, portability,
performance, inconclusive and `--enable=all` checks are not enabled.
See the [official manual](https://cppcheck.sourceforge.io/manual.html).

From a clean public checkout without game-generated code:

```sh
python3 scripts/ci/install_cppcheck.py
cmake -S . -B out/cppcheck -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DPSPRECOMP_PROFILE=mhp3rd -DPSPRECOMP_BUILD_TESTS=ON \
  -DMHP3RD_RENDERER=OFF -DMHP3RD_FFMPEG=OFF
python3 scripts/ci/cppcheck.py
```

The filtered database retains actual compile commands for first-party runtime,
profile host and tests, including each target's include paths and definitions.
The analysis declares C++20 and the Unix 64-bit platform explicitly. Required
vendor headers remain available to the parser, while diagnostics whose primary
location is vendor/generated code do not block first-party changes. No source
suppression or synthetic compile flags hide first-party errors. Reports include
raw XML, progress and a structured first-party summary. The Linux hosted job
is bounded to 25 minutes, with a 15-minute analysis deadline and `-j2`.

This headless configuration excludes Vulkan/SDL renderer branches, FFmpeg,
Android JNI, Windows-specific branches, shaders and game-generated code. It
does not replace compiler warnings, runtime sanitizer tests, clang-tidy or
device validation. Local macOS runs use the Unix model and native compile
commands, rather than claiming Windows/Android analysis coverage.

The runner prepares public headers with the existing CMake generators directly.
It runs only version and NID-table generation from
tracked metadata; it does not compile the game stub, AOT or overlays.

### Cppcheck baseline review

The initial 2.17.1 evaluation analyzed 224 target commands in 52.83 seconds on
Linux and 75.56 seconds on macOS. The final pin uses the current **2.22.0**
release (September 19, 2026), which removed the old false `constStatement`
warning on VIIM's necessary signed-immediate cast. The selected clang-tidy
checks found no first-party issues in the same headless source set. Cppcheck
provides additional bounds/dataflow diagnostics and is tested with a synthetic
out-of-bounds access that must fail the runner.

The current baseline review covers:

- `invalidContainer`, `Kernel::free_block` in `host/kernel/kernel.cpp`: a reviewed false positive.
  `free_block` erases `position + 1`; `position` is before that element and
  remains valid under the [C++ vector erase rules](https://eel.is/c++draft/vector.modifiers#4).
  The runner waives only this diagnostic, file, line and a SHA-256 fingerprint
  of the complete reviewed `free_block` function. The finding remains in raw
  XML and `reviewed_exceptions`. A moved or changed function fails for review.
  A mutation probe changing the erased position verifies that a real invalid
  iterator cannot inherit the exception. Kernel behavior remains unchanged.
- The signal-handler child test's watched texture has static storage to express
  its lifetime explicitly; both expected abort and unexpected success call
  `_Exit`. This is a synthetic harness change, not an application fix.
- The debug test's bounded overlay-name writer now copies the checked prefix
  and pads the remaining bytes in separate loops. The previous strlen-guarded
  ternary was safe but triggered a false bounds error in 2.22.0. It writes the
  same 32-byte field and does not change runtime/game code.
- Advisory `uninitMemberVarNoCtor` warnings concern aggregate records populated
  through explicit initializers (the bindings reference, pack locations,
  user-data items, settings metadata and synthetic test records). They remain
  visible and nonblocking; the adoption does not add constructors or defaults
  across unrelated application code merely to silence the analyzer.

Cppcheck's normal level bounds branch exploration, and `--max-configs=1` bounds
speculative preprocessing per recorded command. Informational limits remain in
reports: a clean result does not mean exhaustive path/configuration coverage.
System C++ APIs use Cppcheck's shipped library models rather than treating its
parser as a replacement compiler frontend.
