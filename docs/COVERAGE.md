# Coverage and regression testing

Every behavior change needs automated tests for its contract. Fixes include a
regression that fails before the fix; new behavior includes successful, failing
and boundary cases. Use public synthetic fixtures rather than game data.
Documentation and mechanical changes need appropriate verification instead of
artificial tests. Record hardware-dependent gaps and manual evidence in the PR.

## Agreed policy

- Run full relevant public test suites on every branch and pull request.
- Require at least **80% coverage of changed executable lines** in addition to
  those suites. Running only tests associated with the diff is insufficient.
- Report overall coverage without an overall minimum during the rollout.
- Keep unmeasured source files visible. Never present coverage of a tested
  subset as coverage of the entire project.

The README coverage badge shows the combined measured source-line percentage
from the latest successful coverage run on `main`, not PR diff coverage. Its
gold and dark colors match the other badges. CI publishes only a Shields endpoint
JSON to the `coverage-badges` data branch; keep that branch. Publication runs in
a separate job with repository write access only on pushes to `main`, without
checking out or executing project code. Branch/PR coverage jobs remain read-only.
The badge becomes available after the first successful main run and can be
cached for several minutes. Its link documents scope and unmeasured files here.

If the badge reports `resource not found`, first check that the data branch and
its `coverage.json` still exist, then check the **Update main coverage badge**
job in the latest successful main coverage run. Do not replace the endpoint with
a hard-coded percentage. A missing branch is recreated by the next successful
main publication; restored JSON and the badge image can remain cached briefly.

## Current implementation and rollout

Public native/Android suites and project
coverage run on pull requests and every pushed branch. Pull requests compare
against their base commit; pushes compare against the previous branch commit,
falling back to `origin/main` for a new branch or a force push whose previous
history may no longer be fetched. The diff gate is an additional
check after full native and Python suites; it does not select a reduced suite.

Install the pinned collector with `make coverage-tools`, then run
`make project-coverage`. To include Android Java locally, first run
`make java-test SDL_SOURCE=/path/to/SDL3` with Maven and Java 17 or newer. The
SDL version and archive checksum are pinned in
`profiles/mhp3rd/packaging/linux/sources.sh`; do not copy SDL sources into Git.
CI downloads and verifies that archive and always runs the Java suite before
the aggregate coverage job. Set `COVERAGE_BASE` to the commit or branch to compare
against locally (the default is `origin/main`). Python reports include branch
coverage and unexecuted first-party source files. The reporter writes JSON and
Markdown inventories under `out/project-coverage`; native and Python HTML
reports are uploaded with the CI artifact. Python collection includes the
native/report orchestration scripts as well as the archive-tool tests.
The collector regression runs the real Make recipes with public fixture
producers and verifies initial suite hits survive later collection/export.
Accumulation uses `coverage combine --append`; replacing the merged database
between phases would lose earlier hits.

`make coverage` builds instrumented native test binaries, the public application
stub and command-line tools. It runs the full native suite before collecting
reports; a failed test stops collection. The coverage build uses the empty
generated-code registry even when a local generated corpus exists. It does not
compile or upload game-derived code. Native coverage uses LLVM source mapping
and exports LCOV for the diff check.

Coverage CI compiles the application with the Vulkan renderer and system
FFmpeg enabled, including their first-party source mappings even where the
tests execute synthetic Vulkan/SDL/ImGui contracts alongside headless suites. It builds the same pinned SDL source
used by the Java suite; vendor SDL/FFmpeg code is excluded from the first-party
report. Requested renderer coverage fails configuration if its dependencies
are missing instead of silently reducing the denominator. Locally, use
`make project-coverage CMAKE_ARGS="-DMHP3RD_RENDERER=ON -DMHP3RD_FFMPEG=system -DMHP3RD_RENDERER_TESTS=ON"`
after installing the public dependencies. The default local command remains
headless so it can run without them.

The project coverage reporter inventories tracked first-party native, Python,
Android Java, shader and build sources. It can combine native LCOV, coverage.py
JSON and JaCoCo XML. Reports show measured files and remaining gaps separately
per language. An unmapped changed native/Python/Java file fails the
gate rather than silently disappearing from its denominator. Deleted lines and
unchanged lines do not enter the changed-line count.

Declaration-only headers can have no standalone LLVM line mapping even when
all changed behavior is exercised in their implementations. The reviewed
`scripts/ci/coverage_unmapped_headers.json` manifest records their exact SHA-256
fingerprints and verification rationale. They remain visible as unmeasured in
the inventory and are listed separately in the diff report; no percentage is
invented for them. Any edit invalidates the review until its fingerprint and
rationale are reviewed again. Emitted mappings always use the normal 80% gate,
and unknown implementation files still fail. Regression tests verify source
mutation and mapped-line failures cannot bypass enforcement.
The combined measured source-line total pools covered/executable line counts
across languages, rather than averaging percentages. It has no minimum and
excludes unknown lines; the report states this limitation alongside the value.

The CI foundation does not close the remaining subsystem gaps. Expand tests in
stages, using the reports to choose contracts and regressions rather than tests
that merely execute lines:

| Stage | Required next checks |
| --- | --- |
| Kernel and HLE | Allocation/free/coalescing, scheduler context switches, synchronization and timeouts, interrupts, I/O boundary errors |
| Runtime and code generation | Unsupported and boundary instructions, guest dispatch ownership, relocation/ELF errors and synthetic generated-code execution |
| Networking | HLE wait/cancel contracts, malformed protocol messages, reconnect and bounded multiplayer scenarios |
| Audio/media | Synthetic PCM/decoder input, malformed streams, timing and buffer ownership; separate hardware playback evidence |
| Renderer and UI | GE decoding/state contracts, descriptor lifetime, texture/cache boundaries and deterministic offscreen/UI scenarios |
| Tools/platforms | Remaining Python orchestration paths, Android lifecycle/cutout paths, Ghidra integration, Windows/Android-specific native mappings |

Retain unmeasured platform/hardware scenarios in the inventory and PR evidence
throughout the rollout. No percentage alone marks a subsystem complete.

The project reporter has format and integration tests against real Git
histories, including the exact threshold, a failing threshold, missing Java
mapping, deletion-only hunks and a new-branch base. The ad hoc exit fixture now
matches the production server's process lifetime and separately verifies an
owned server's callback users are joined before destruction.

Android Java tests run under Robolectric on API 29 and 35. They check landscape
orientation, document selection/cancellation, tree URIs, folder listing and
cursor cleanup, denied document access, safe JNI defaults and message-box
buttons/keyboard handling. Additional contracts cover cutout insets independently
of system bars, the cutout window policy during unavailable native startup,
narrow/wide button-bar layouts, Enter key release, missing keyboard defaults,
UI-thread haptic feedback and a missing relaunch entry. Android system insets
are supplied by a narrow synthetic window-root fixture; no physical display or
native SDL startup is claimed. JaCoCo instruments the original application bytecode
before Robolectric transforms it and restores the classes before reporting;
SDL and test classes are excluded from the application coverage denominator.
These JVM tests complement the native Android emulator suite and do not claim
SDL native startup, a rendered game or a hardware test. The Ghidra Java tool
remains explicitly unmeasured in the inventory.

Shader tests compile all seven renderer variants from the four public GLSL
sources, verify named SPIR-V arrays and check failed-compilation diagnostics and
temporary-file cleanup. Coverage CI installs `glslangValidator`; other test
environments explicitly skip these checks if the compiler is unavailable.
This validates shader compilation and embedding, not rendered GPU behavior.

## Interpreting reports

Line coverage records whether executable lines ran; it does not prove their
behavior was asserted. Branch coverage adds useful information about decisions,
but neither metric replaces regression tests or manual platform verification.

Headless Linux or macOS execution does not measure every platform-specific
path, rendering behavior, game integration or multiplayer scenario. Unmapped
files have an unknown executable-line count and cannot be folded into a claimed
whole-project percentage. Shaders and build scripts need relevant compilation
and behavioral checks; the current reporter does not fabricate line coverage
for them. Third-party code, generated game code, game data and test fixtures are
outside the first-party production coverage scope.
The aggregate native inventory and diff count unique source lines from LCOV.
The separate LLVM report preserves LLVM's own line summaries; macro/template
mappings can produce different denominators. Compare the same report format
and configuration when assessing changes.

See [TESTING.md](TESTING.md) for existing public checks and manual smoke tests,
and [CONTRIBUTING.md](../CONTRIBUTING.md#testing-changes) for contributor rules.

## Expanded synthetic contracts

The public test aggregate includes kernel/HLE and media contracts plus reusable
runtime, memory, interpreter, ELF and recompiler workflows. The renderer/UI
suite is opt-in with `MHP3RD_RENDERER_TESTS=ON` and uses real SDL, Vulkan and
ImGui with independently constructed pixels, archives and temporary settings.
It exercises default, checked, conservative and documented rollback paths,
scripted input, UI restart and a bounded texture cache. Checked replay requires
an actual zero-difference pixel report; diagnostic mismatches fail the test.

Linux coverage uses Xvfb and Mesa software Vulkan, the pinned SDL dependency
with X11 support, and an installed system font. This complements physical GPU
and gameplay testing; it does not establish Windows/Android rendering coverage
or device-loss recovery. Preserve those gaps in the report. Audio, camera and
media results must be combined with their other suites rather than quoting one
executable's subset as the module total.

Set `PSPRECOMP_TEST_RECOMP` to the built public `psp_recomp` executable before
Python collection to run the real synthetic overlay-generation workflow.
This generates public fixture code without rebuilding game overlays. Android's
native runner preserves CTest variant arguments and environment assignments,
including guest-memory write-watch contracts sharing one executable.
