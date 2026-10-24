#!/usr/bin/env python3
"""Run every registered native CTest executable on one Android emulator."""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import time
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("stl", type=Path)
    args = parser.parse_args()
    build = args.build_dir.resolve(strict=True)
    stl = args.stl.resolve(strict=True)
    if not build.is_dir() or not stl.is_file() or stl.name != "libc++_shared.so":
        raise ValueError("Expected a build directory and the NDK libc++_shared.so file")
    # Keep executables fixed and filesystem arguments absolute: a caller's path
    # cannot select a program or become an adb command-line option.
    tests = json.loads(subprocess.run(
        ["ctest", "--test-dir", str(build), "--show-only=json-v1"],
        check=True, timeout=120, capture_output=True, text=True).stdout)["tests"]
    if not tests:
        raise RuntimeError("No CTest tests registered")
    nids = Path(__file__).resolve().parents[2] / "configs/nids.csv"
    if not nids.is_file():
        raise ValueError("Missing public NID registry fixture")
    binaries = {build / "psp_recomp"}
    names = set()
    for test in tests:
        name = test["name"]
        binary = Path(test["command"][0]).resolve(strict=True)
        if not re.fullmatch(r"[A-Za-z0-9_]+", name) or name in names:
            raise ValueError("CTest names must be unique identifiers")
        if not binary.is_relative_to(build) or not binary.is_file() or not re.fullmatch(r"[A-Za-z0-9_]+", binary.name):
            raise ValueError("CTest executables must have safe names and belong to the build directory")
        environment = []
        for prop in test.get("properties", []):
            if prop["name"] == "ENVIRONMENT":
                for assignment in prop["value"]:
                    key, separator, _ = assignment.partition("=")
                    if not separator or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", key):
                        raise ValueError("Invalid CTest environment assignment")
                    environment.append(assignment)
        test["remote_environment"] = environment
        names.add(name)
        binaries.add(binary)
    remote = "/data/local/tmp/yakumo-unit-tests"
    diagnostics = build / "android-results"
    diagnostics.mkdir(exist_ok=True)
    suite = ET.Element("testsuite", name="Android native tests", tests=str(len(tests)))
    failed = 0
    try:
        subprocess.run(["adb", "shell", f"rm -rf {remote} && mkdir -p {remote}/tmp {remote}/home"],
                       check=True, timeout=120)
        for binary in sorted(binaries):
            subprocess.run(["adb", "push", str(binary), remote + "/"], check=True, timeout=120)
        subprocess.run(["adb", "push", str(stl), remote + "/libc++_shared.so"], check=True, timeout=120)
        subprocess.run(["adb", "push", str(nids), remote + "/nids.csv"], check=True, timeout=120)
        subprocess.run(["adb", "shell", f"chmod 755 {remote}/*tests {remote}/psp_recomp"],
                       check=True, timeout=120)
        for test in tests:
            name = test["name"]
            command = [remote + "/" + Path(test["command"][0]).name, *test["command"][1:]]
            shell = (f"cd {remote} && export LD_LIBRARY_PATH={remote} TMPDIR={remote}/tmp "
                     f"HOME={remote}/home PSPRECOMP_CODEGEN_PATH={remote}/psp_recomp "
                     f"PSPRECOMP_TEST_NIDS_CSV={remote}/nids.csv && "
                     "timeout 120 " + shlex.join(["env", *test["remote_environment"], *command]))
            start = time.monotonic()
            try:
                result = subprocess.run(["adb", "shell", shell], capture_output=True,
                                        text=True, timeout=135)
                output = result.stdout + result.stderr
                code = result.returncode
            except subprocess.TimeoutExpired as error:
                output = f"ADB did not return within 135 seconds: {error}"
                code = 124
            elapsed = time.monotonic() - start
            (diagnostics / f"{name}.log").write_text(output)
            case = ET.SubElement(suite, "testcase", name=name, time=f"{elapsed:.3f}")
            ET.SubElement(case, "system-out").text = output
            print(f"{name}: {'PASS' if code == 0 else 'FAIL'} ({elapsed:.2f}s)", flush=True)
            if code:
                failed += 1
                ET.SubElement(case, "failure", message=f"Exit status {code}").text = output
                print(output, flush=True)
    finally:
        suite.set("failures", str(failed))
        ET.ElementTree(suite).write(diagnostics / "test-results.xml", encoding="utf-8", xml_declaration=True)
        subprocess.run(["adb", "shell", f"rm -rf {remote}"], check=True, timeout=120)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
