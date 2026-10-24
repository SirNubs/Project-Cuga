#!/usr/bin/env python3
"""Compile Android Java for CodeQL without native code, assets or APK packaging."""

import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("android_jar", type=Path)
    parser.add_argument("sdl_source", type=Path)
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--stage", choices=("all", "sdl", "app"), default="all")
    args = parser.parse_args()
    android_jar = args.android_jar.resolve(strict=True)
    sdl_java = args.sdl_source.resolve(strict=True) / "android-project/app/src/main/java"
    app_java = Path(__file__).resolve().parents[2] / "profiles/mhp3rd/packaging/android/java"
    build_dir = args.build_dir.resolve()
    sdl_classes = build_dir / "sdl-classes"
    app_classes = build_dir / "app-classes"

    # Prepare SDL before CodeQL init; trace only Yakumo's compilation afterward.
    # SDL bytecode and android.jar remain available for resolving external calls.
    for stage, source_dir, output_dir, classpath in (
        ("sdl", sdl_java, sdl_classes, str(android_jar)),
        ("app", app_java, app_classes, os.pathsep.join((str(android_jar), str(sdl_classes)))),
    ):
        if args.stage not in ("all", stage):
            continue
        sources = sorted(source_dir.rglob("*.java"))
        if not sources:
            raise SystemExit(f"No Java sources found in {source_dir}")
        if stage == "app" and not (sdl_classes / "org/libsdl/app/SDLActivity.class").is_file():
            raise SystemExit("Compile the SDL stage before the app stage")
        output_dir.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            ["javac", "--release", "11", "-classpath", classpath, "-d", str(output_dir)]
            + [str(path) for path in sources],
            check=True,
        )
        print(f"Compiled {len(sources)} {stage} Java sources", flush=True)


if __name__ == "__main__":
    main()
