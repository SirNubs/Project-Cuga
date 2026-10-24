#!/usr/bin/env python3
"""Validate the configured sanitizer instrumentation with disposable bad inputs."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()
    build = args.build_dir.resolve(strict=True)
    cache = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        if line and not line.startswith(("#", "//")) and "=" in line:
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    if cache.get("PSPRECOMP_SANITIZERS") != "ON":
        raise RuntimeError("The supplied build must enable PSPRECOMP_SANITIZERS")
    for entry in json.loads((build / "compile_commands.json").read_text()):
        command = shlex.split(entry["command"])
        if "-fsanitize=address,undefined" not in command or "-fno-sanitize-recover=all" not in command:
            raise RuntimeError(f"Missing sanitizer compile instrumentation: {entry['file']}")

    module = Path(cache["CMAKE_HOME_DIRECTORY"]) / "cmake/Sanitizers.cmake"
    diagnostics = build / "sanitizer-probes"
    diagnostics.mkdir(exist_ok=True)
    # These examples exist only in a temporary project, never as normal CTest
    # targets. Each process must fail with its specific diagnostic, not timeout.
    probes = {
        "address": ("#include <cstdlib>\n#include <csignal>\nint main() { std::signal(SIGABRT, [](int) { std::_Exit(0); }); auto* p = static_cast<volatile char*>(std::malloc(1)); std::free(const_cast<char*>(p)); return *p; }\n", "AddressSanitizer: heap-use-after-free"),
        "undefined": ("#include <climits>\nint main(int argc, char**) { volatile int value = INT_MAX; return value + argc; }\n", "runtime error: signed integer overflow"),
        "leak": ("#include <cstdlib>\nint main() { void* p = std::malloc(16); asm volatile(\"\" : : \"r\"(p) : \"memory\"); p = nullptr; }\n", "LeakSanitizer: detected memory leaks"),
    }
    with tempfile.TemporaryDirectory(prefix="sanitizer-probes-", dir=build) as temporary:
        source = Path(temporary)
        (source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.20)\n"
            "project(SanitizerProbes LANGUAGES CXX)\n"
            'include("${SANITIZER_MODULE}")\n'
            + "".join(f"add_executable({name} {name}.cpp)\n" for name in probes)
        )
        for name, (code, _) in probes.items():
            (source / f"{name}.cpp").write_text(code)
        commands = [
            ["cmake", "-S", str(source), "-B", str(source / "out"), "-G", "Ninja",
             f"-DCMAKE_CXX_COMPILER={cache['CMAKE_CXX_COMPILER']}",
             f"-DSANITIZER_MODULE={module}", "-DPSPRECOMP_SANITIZERS=ON",
             "-DCMAKE_BUILD_TYPE=Debug"],
            ["cmake", "--build", str(source / "out"), "-j2"],
        ]
        for index, command in enumerate(commands):
            result = subprocess.run(command, capture_output=True, text=True, timeout=120)
            (diagnostics / f"build-{index}.log").write_text(result.stdout + result.stderr)
            result.check_returncode()
        for name, (_, expected) in probes.items():
            result = subprocess.run([str(source / "out" / name)], capture_output=True,
                                    text=True, timeout=30)
            output = result.stdout + result.stderr
            (diagnostics / f"{name}.log").write_text(output)
            if result.returncode == 0 or expected not in output:
                raise RuntimeError(f"{name} probe did not fail with its expected sanitizer report")
            print(f"{name}: expected sanitizer rejection confirmed", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
