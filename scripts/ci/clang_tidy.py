#!/usr/bin/env python3
"""Analyze first-party translation units using the actual CMake database."""

import argparse
import json
import re
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOTS = ('src/', 'tests/', 'profiles/mhp3rd/host/', 'profiles/mhp3rd/tests/')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timeout', type=int, default=120, help='seconds per translation unit')
    args = parser.parse_args()
    # Invoke the native analyzer directly: the wheel's Python console wrapper
    # forks a child which subprocess.run's timeout would otherwise orphan.
    binaries = list(ROOT.glob('out/tidy-tools/lib/python*/site-packages/clang_tidy/data/bin/clang-tidy'))
    if len(binaries) != 1:
        parser.error('install the pinned analyzer in out/tidy-tools first')
    tool = binaries[0]
    version = subprocess.check_output([str(tool), '--version'], text=True)
    if not re.search(r'\bLLVM version 22\.1\.8\b', version):
        parser.error('clang-tidy 22.1.8 is required')
    build = ROOT / 'out/tidy'
    # Reuse the build's public header generators directly. Requesting Ninja's
    # NID output target also pulls in object-library dependencies unnecessarily.
    subprocess.run(['cmake', f'-DINPUT={ROOT / "configs/nids.csv"}',
                    f'-DOUTPUT={build / "profiles/mhp3rd/generated_nids/nid_table.inc"}',
                    '-P', str(ROOT / 'profiles/mhp3rd/tools/embed_nids.cmake')],
                   check=True, timeout=30)
    subprocess.run(['cmake', f'-DSOURCE_DIR={ROOT}',
                    f'-DOUTPUT={build / "profiles/mhp3rd/generated_version/yakumo_version.hpp"}',
                    '-P', str(ROOT / 'profiles/mhp3rd/tools/write_version.cmake')],
                   check=True, timeout=30)
    database = json.loads((build / 'compile_commands.json').read_text())
    files = set()
    excluded = set()
    for entry in database:
        source = (Path(entry['directory']) / entry['file']).resolve()
        try:
            relative = source.relative_to(ROOT).as_posix()
        except ValueError:
            excluded.add(str(source))
            continue
        if relative.startswith(SOURCE_ROOTS):
            files.add(relative)
        else:
            excluded.add(relative)
    if not files:
        parser.error('database has no first-party translation units')
    # clang-tidy retains each source's definitions, standard and include paths
    # from the database, including multiple target configurations for a file.
    output = build / 'clang-tidy-results'
    output.mkdir(parents=True, exist_ok=True)
    (output / 'coverage.json').write_text(json.dumps(
        {'analyzed': sorted(files), 'excluded': sorted(excluded), 'version': version}, indent=2) + '\n')
    started = time.monotonic()
    failures = []
    for index, source in enumerate(sorted(files), 1):
        command = [str(tool), '-p', str(build), '--config-file', str(ROOT / '.clang-tidy'),
                   '--quiet', str(ROOT / source)]
        try:
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, timeout=args.timeout, check=False)
            text = result.stdout
            failed = result.returncode != 0
        except subprocess.TimeoutExpired as error:
            text = f'Timed out after {args.timeout}s: {error}\n'
            failed = True
        (output / f'{index:03}.log').write_text(f'{source}\n{text}')
        print(f'[{index}/{len(files)}] {source}: {"FAIL" if failed else "OK"}', flush=True)
        if failed:
            failures.append(source)
            print(text, flush=True)
    summary = {'translation_units': len(files), 'failed': failures,
               'elapsed_seconds': round(time.monotonic() - started, 2)}
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary), flush=True)
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
