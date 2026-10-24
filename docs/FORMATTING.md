# C++ formatting

The first-party C++ style was approved before the initial mechanical formatting
commit. CI now checks the complete scoped baseline with the pinned tool. See [#251](https://github.com/TeamGDB/Yakumo/issues/251)
and its parent [#249](https://github.com/TeamGDB/Yakumo/issues/249).

## Tool and scope

Use **clang-format 21.1.8**. The [LLVM release](https://github.com/llvm/llvm-project/releases/tag/llvmorg-21.1.8)
has binaries for Linux, Apple Silicon macOS and Windows. The
[LLVM formatter documentation](https://releases.llvm.org/21.1.0/tools/clang/docs/ClangFormat.html)
describes configuration files and editor integration. A formatter version does
not select the compiler used to build Yakumo: GCC, MSVC and Clang builds continue
to use their existing toolchains.

For a smaller cross-platform installation, the commands below use the
[clang-format Python package](https://pypi.org/project/clang-format/21.1.8/), which
packages LLVM's formatter separately from LLVM's official release downloads.
`scripts/requirements-format.txt` pins the version and SHA-256 of the published
binary wheels; `--only-binary` prevents a source-build fallback. The wrapper also
rejects any executable whose reported formatter version differs.

From the repository root on macOS/Linux:

```sh
python3 -m venv out/format-venv
out/format-venv/bin/python -m pip install --only-binary=:all: --require-hashes -r scripts/requirements-format.txt
. out/format-venv/bin/activate
python scripts/format_cpp.py report --samples --diff
```

On Windows, use the equivalent virtual environment executables:

```powershell
py -3 -m venv out/format-venv
out/format-venv/Scripts/python.exe -m pip install --only-binary=:all: --require-hashes -r scripts/requirements-format.txt
& out/format-venv/Scripts/Activate.ps1
python scripts/format_cpp.py report --samples --diff
```

The wrapper selects only Git-tracked regular C/C++ files in `include/psprecomp`,
`src`, `tests`, `tools`, `profiles/mhp3rd/host` and `profiles/mhp3rd/tests`.
It excludes vendored code, generated code, game data, overlays and build outputs,
including nested excluded directories and symlinks. Explicit file paths must be
in this scope; untracked files must be staged before they can be selected.
The `.clang-format-ignore` file also protects direct file invocations. Use the
wrapper for bulk operations: editor integrations and stdin invocations may not
respect ignore-file discovery in the same way.

## Adopted style

The inspected samples cover a runtime header and implementation, a host input
module, profile and framework tests, and a command-line tool. Existing code
usually uses four spaces, attached braces, unindented namespaces and pointer
stars beside the variable, but mixes line widths and compact control flow.
The adopted policy retains those common choices:

| Choice | Policy |
| --- | --- |
| Indentation | Four spaces, no tabs; four-space continuation and braced-initializer indentation |
| Wrapped expressions | Fixed continuation indentation rather than horizontal alignment with opening brackets or operands |
| Braces and namespaces | Attached braces; namespaces do not indent their contents |
| Pointer and reference placement | `Type *pointer`, `Type &reference` |
| Line width | 120 columns; clang-format may leave indivisible tokens longer |
| Compact statements | Short `if` without `else` and short loops may stay on one line |
| Functions | Short class-inline functions may stay on one line; ordinary function bodies expand |
| Includes and using declarations | Preserve ordering and blocks, including platform-dependent ordering |
| Comments | Do not reflow text or add namespace comments |
| Code transformations | Do not insert braces, reorder qualifiers or split string literals |

The initial scoped rewrite follows the reviewed four-space continuation policy,
120-column width and compact-control-flow choices. It keeps literals intact and
does not sort using declarations. Before the mechanical commit, the earlier
proposal differed in 215 of 260 scoped files; after adoption, **all 260 match**.
That inventory measures formatting, not code defects.

The mechanical change was checked with Clang raw lexical tokens across all 260
files, retaining literal spellings, preprocessor directive boundaries and
function-like macro adjacency. Whitespace, comments and source locations are
excluded from token comparison. This is stronger than stripping whitespace
inside strings, but is not a substitute for compilation or device tests.

## Local checks and enforcement

Put the exact formatter on PATH, for example by activating the environment above.
The wrapper invokes the fixed command `clang-format`; it does not accept an
arbitrary executable from command-line input.

```sh
# Preview all existing differences; does not write source or fail for differences.
python3 scripts/format_cpp.py report --json
# Preview the representative source set.
python3 scripts/format_cpp.py report --samples --diff > out/format-review/samples.diff
# Check the complete first-party scope (also used by CI).
python3 scripts/format_cpp.py check
# Check just explicit files (exit 1 if formatting differs).
python3 scripts/format_cpp.py check profiles/mhp3rd/tests/guest_pcm_tests.cpp --diff
# Select changed files since the merge-base with main, including local tracked edits.
python3 scripts/format_cpp.py check --base origin/main --diff
# Format the complete scope; keep mechanical changes in their own commit.
python3 scripts/format_cpp.py format
# Or format one staged/committed file.
python3 scripts/format_cpp.py format profiles/mhp3rd/tests/guest_pcm_tests.cpp
```

`--json` writes the fixed repository-local `out/format-review/baseline.json`
path; arbitrary report output paths are not accepted.

`check` returns 0 for conforming files, 1 for formatting differences and 2 for a
tool/version/input failure. `report` returns 0 for differences but still returns
2 on a tool failure. `format` writes only selected source files. Without paths,
`--base` or `--samples`, any mode selects the full scoped baseline.

The separate GitHub Actions job checks **the complete first-party scope**.
Formatting differences and formatter/tool failures both fail the job. It uses
ordinary hosted runners, reads no game data and builds no game code or overlays.
It does not change repository branch-protection settings or release builds.

New tracked first-party files must match this policy, as must changes to existing
files. For staged work, `--base` remains a useful smaller local check; CI checks
all scoped files so a configuration change cannot silently invalidate untouched
files. Keep mechanical formatting in dedicated commits, separate from behavioral
changes. Configuration/version changes require fresh sample review and an
idempotent baseline. No suppression list is used.
