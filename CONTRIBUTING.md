# Contributing to Yakumo

Bug reports, documentation improvements, compatibility testing and code contributions are welcome. Follow the [Code of Conduct](CODE_OF_CONDUCT.md) when participating in repository discussions.

## Reporting problems

Check existing issues before opening a new one. Use the Bug report form for problems, Feature request for proposed improvements, or Test report for smoke-test results. Include the following when reporting a problem:

- The release version or exact commit you tested.
- Your platform and relevant hardware.
- Steps to reproduce the problem, expected behavior and actual behavior.
- Relevant logs or screenshots, with personal information removed.

Use minimal synthetic examples where possible. Keep game files and saves local; do not attach them to issues or pull requests. Report suspected security vulnerabilities privately as described in [SECURITY.md](SECURITY.md).

## Contribution rules

- Write code, comments, documentation, commit messages and pull requests in English. The exceptions are `README.ru.md` and `README.es.md`; update these translations in the same pull request as `README.md` when changing its content.
- Never commit game data: disc images, `EBOOT` or `DATA.BIN` contents, generated game code, overlay corpora or saves. Do not include personal paths, user names, machine names, device addresses or credentials.
- Do not share pirated copies or distribute Yakumo builds bundled with copyrighted game content through project spaces. See the [game-content policy](CODE_OF_CONDUCT.md#piracy-and-game-content).
- Write implementations yourself. Do not copy or translate code line by line from projects with licenses incompatible with this repository's MIT license. Public documentation, constants, offsets and format facts can inform your work. Record the origin of intentionally included third-party code as described in [SOURCE_PROVENANCE.md](docs/SOURCE_PROVENANCE.md).

## Getting started

Start with [ARCHITECTURE.md](docs/ARCHITECTURE.md) to understand the runtime and game-profile boundaries.

Follow [BUILDING.md](docs/BUILDING.md) for your platform. Full builds are expensive: reuse local generated code, compatible prebuilt overlays and game data as the guide describes. Runtime-header changes can require rebuilding all overlays.

Build with `cmake --build`, rather than invoking Ninja directly. Run only one build per build directory and keep parallelism low (`-j2`). Do not delete `.ninja_deps` or `.ninja_log`.

If you have a device-specific setup, keep its paths, addresses and launch instructions in the ignored `LOCAL_TESTING.md` at the repository root. Read it before using that environment; it is optional and is not part of a fresh clone.

## Testing changes

Every behavior change must include automated tests for the changed contract.
Bug fixes include a regression that fails before the fix and passes afterward;
new features exercise successful behavior and relevant failure/boundary cases.
Use synthetic public inputs, deterministic assertions and bounded execution.
Do not replace behavioral tests with implementation-shaped mocks or increase
coverage by removing untested sources, weakening assertions or exclusions.

Run the full relevant suites before requesting review. CI runs the full public
platform suites on branches and pull requests, with an additional 80% changed
executable-line coverage requirement for measured native and Python sources.
Changed Java files without a coverage mapping also fail the gate; see the
rollout status below for remaining coverage gaps. A diff coverage check
complements full suite testing; it does not replace regression checks outside
the diff.

Documentation-only and purely mechanical changes need appropriate verification
rather than artificial code tests. When hardware/game data makes automation
unavailable, add tests for the separable logic and document the remaining manual
scenario, evidence and coverage gap in the pull request. Do not claim unmeasured
code is covered. See [COVERAGE.md](docs/COVERAGE.md) for scope and rollout.

Choose checks that exercise the behavior you changed. [TESTING.md](docs/TESTING.md) describes automated checks and manual smoke tests; [COMPATIBILITY.md](docs/COMPATIBILITY.md) records verified platform results.

On macOS/Linux with Make, the root `Makefile` provides shortcuts to the existing
CMake and Python commands. `make` lists the available targets. Start with:

```sh
make configure
make test
make python-test
make tools
make check
```

The default build directory is `out/tests`, with headless profile tests and no
game data. `make build` builds only test binaries; `make test` builds them before
running CTest. Configure once before using either command. `make check` runs Ruff,
the complete C++ formatting check, native tests and Python tests sequentially;
tool installation is explicit through `make tools`. Builds default to two jobs.
Override `BUILD_DIR`, `JOBS`, `CMAKE_ARGS` or `CTEST_ARGS` as needed.

`make format` changes scoped first-party source files. The heavier analyses are
separate: `make tidy-tools` then `make tidy`, `make cppcheck-tools` then
`make cppcheck`, and `make sanitizers` with the documented native Linux Clang
toolchain. See [TESTING.md](docs/TESTING.md) for prerequisites, including the
Apple SDK flags for clang-tidy. These targets use their existing separate
`out/` directories. `make app APP_BUILD_DIR=out/mhp3rd` builds the application
only after that directory has been configured following the build guide; reuse
compatible generated objects and overlays as usual.

`make coverage` runs native suites with Clang coverage in its own build
directory, including the public application stub and tools. Install the pinned
Python collector with `make coverage-tools`, then run `make project-coverage`
for the native/Python inventory and 80% diff check. `make java-test` adds Android
Java reports when Maven, Java and the pinned SDL source are available. See
[COVERAGE.md](docs/COVERAGE.md) for full-renderer CI, commands and remaining gaps.

Make is optional. Windows users can use the direct CMake/Python commands below
and in the testing guide; the convenience recipes require a POSIX shell and
Unix-style virtual environment paths. The Linux coverage job uses these recipes;
the desktop/Android native jobs retain direct CMake commands.

For the unit-test targets:

```sh
cmake --build out/mhp3rd --target psprecomp_test_binaries -j2
ctest --test-dir out/mhp3rd
```

Bound game runs with a timeout and inspect logs and captures. For regressions, compare a known-good build and your change with the same scripted input and game state. Measure performance changes before and after rather than relying on impressions. See [TESTING.md](docs/TESTING.md) for testing workflows and the profile README's [Diagnostics](profiles/mhp3rd/README.md#diagnostics) section for tracing options.

## Python tooling

Install the pinned development tools in a virtual environment (Python 3.9 or newer):

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements/dev.txt
.venv/bin/ruff check .
```

On Windows use `.venv\Scripts\python.exe` and `.venv\Scripts\ruff.exe`. Activating the environment also lets you run `python` and `ruff` directly.

Ruff checks first-party Python under `scripts/`, `profiles/mhp3rd/tools/` and `profiles/mhp3rd/tests/`. `ruff.toml` explicitly excludes vendored code, generated code, game directories, overlays and build outputs, including when a path is passed explicitly. The initial rule set checks Pyflakes diagnostics (`F`), syntax errors (`E9`) and bare `except` clauses (`E722`). Python formatting, import ordering and broad style rules are not enforced at this stage. The configuration requires the exact version pinned in `requirements/dev.txt`.

The `Python lint (Ruff)` job runs the same configuration on public GitHub-hosted CI for pull requests and pushes to main or release branches. Findings fail the job; review and fix them before requesting a merge. The check needs no game data or build. Changes to branch protection and checks for C++ formatting, clang-tidy, Cppcheck and sanitizers are separate stages in [#249](https://github.com/TeamGDB/Yakumo/issues/249).

## C++ formatting

[FORMATTING.md](docs/FORMATTING.md) describes the pinned clang-format 21.1.8
commands and first-party scope. Run `python3 scripts/format_cpp.py check` with
the pinned formatter on PATH before submitting C++ changes; CI checks the full
scoped baseline. Keep mechanical formatting in dedicated commits, separate from
behavior changes. Review style/version changes before reformatting the baseline.

## Submitting a pull request

1. Work on a branch or fork and open a pull request against `main`. Never push directly to `main`.
2. Open a draft pull request early and push working commits so the work is available for review.
3. Keep the change focused and the commit series clean and reviewable. Pull requests are merged by rebase.
4. Explain what changed and why, reference related issues, and list validation results: platform, tested commit, checks performed and anything not verified.
5. Update documentation affected by the change, including settings, build instructions and verified compatibility results where applicable.
6. Leave the pull request in draft while it is still being prepared. Request review when it is ready; merging requires an explicit maintainer decision.

Fixes go to `main` first and are backported to release branches when applicable. Follow [RELEASING.md](docs/RELEASING.md#branches) for the release workflow.
