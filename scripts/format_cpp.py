#!/usr/bin/env python3
"""Check or format tracked first-party C/C++ with a pinned clang-format."""

import argparse
import difflib
import json
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath

VERSION = "21.1.8"
ROOTS = (
    "include/psprecomp/", "src/", "tests/", "tools/",
    "profiles/mhp3rd/host/", "profiles/mhp3rd/tests/",
)
SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
EXCLUDED = {"third_party", "generated", "game", "overlays", "overlay_corpora", "out", "build"}
SAMPLES = (
    "include/psprecomp/guest_memory.hpp",
    "src/guest_memory.cpp",
    "profiles/mhp3rd/host/ui/input_script.cpp",
    "profiles/mhp3rd/tests/guest_pcm_tests.cpp",
    "tests/test_main.cpp",
    "tools/dump_function.cpp",
)


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args])


def tracked_sources(root):
    result = set()
    # Use index modes to reject symlinks and submodules without following them.
    for entry in git(root, "ls-files", "--stage", "-z").split(b"\0"):
        if not entry:
            continue
        metadata, raw_name = entry.split(b"\t", 1)
        mode, _, stage = metadata.split()
        name = raw_name.decode("utf-8")
        path = PurePosixPath(name)
        if (mode in {b"100644", b"100755"} and stage == b"0"
                and name.startswith(ROOTS) and path.suffix.lower() in SUFFIXES
                and not EXCLUDED.intersection(path.parts)):
            result.add(name)
    return result


def run(args):
    root = Path(subprocess.check_output(
        ["git", "rev-parse", "--show-toplevel"], text=True).strip())
    version = subprocess.check_output(["clang-format", "--version"], text=True).strip()
    if not re.search(r"\bclang-format version " + re.escape(VERSION) + r"(?:\s|$)", version):
        raise ValueError(f"Expected clang-format {VERSION}; found {version}")
    eligible = tracked_sources(root)
    if args.samples:
        selected = set(SAMPLES)
        if not selected <= eligible:
            raise ValueError("A formatting sample is absent from the tracked source scope")
    elif args.paths:
        # Paths are root-relative, even when invoked from a subdirectory.
        invalid = set(args.paths) - eligible
        if invalid:
            raise ValueError("Outside tracked first-party scope: " + ", ".join(sorted(invalid)))
        selected = {name for name in eligible if name in args.paths}
    elif args.base:
        base = git(root, "merge-base", args.base, "HEAD").decode().strip()
        names = git(root, "diff", "--name-only", "--diff-filter=ACMR", "-z", base)
        changed_names = {n.decode("utf-8") for n in names.split(b"\0") if n}
        selected = {name for name in eligible if name in changed_names}
    else:
        selected = eligible
    changed = []
    checked = 0
    for name in sorted(selected):
        path = root / name
        # Reject a working-tree symlink too, including any symlink parent.
        if any(p.is_symlink() for p in (path, *path.parents)):
            raise ValueError(f"Refusing symlink path: {name}")
        if not path.is_file():
            if args.paths or args.samples:
                raise ValueError(f"Missing source: {name}")
            continue  # A tracked file deleted in the worktree needs no formatting.
        original = path.read_bytes()
        formatted = subprocess.check_output([
            "clang-format", f"--style=file:{root / '.clang-format'}",
            f"--assume-filename={name}", "--Werror",
        ], input=original, cwd=root)
        checked += 1
        if original == formatted:
            continue
        changed.append(name)
        if args.diff:
            before = original.decode("utf-8").splitlines(keepends=True)
            after = formatted.decode("utf-8").splitlines(keepends=True)
            sys.stdout.writelines(difflib.unified_diff(before, after, f"a/{name}", f"b/{name}"))
        if args.mode == "format":
            path.write_bytes(formatted)
    summary = {"version": VERSION, "checked": checked, "different": len(changed), "files": changed}
    if args.json:
        output = root / "out/format-review/baseline.json"
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"clang-format {VERSION}: {checked} checked, {len(changed)} differ ({args.mode})", file=sys.stderr)
    # Report is advisory only for formatting differences, never for tool errors.
    return 1 if changed and args.mode == "check" else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("check", "report", "format"))
    parser.add_argument("paths", nargs="*", help="exact tracked paths relative to repository root")
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--base", help="select files changed since merge-base with this ref")
    selection.add_argument("--samples", action="store_true", help="select six review samples")
    parser.add_argument("--diff", action="store_true", help="print proposed unified diffs")
    parser.add_argument("--json", action="store_true",
                        help="write out/format-review/baseline.json")
    args = parser.parse_args()
    if args.paths and (args.base or args.samples):
        parser.error("choose paths, --base or --samples, not more than one")
    try:
        return run(args)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Formatting failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
