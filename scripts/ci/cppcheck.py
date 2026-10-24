#!/usr/bin/env python3
"""Cppcheck's independent bounds, lifetime and dataflow diagnostics."""

import argparse
import hashlib
import json
import os
import signal
import subprocess
import time
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOTS = ('src/', 'tests/', 'profiles/mhp3rd/host/', 'profiles/mhp3rd/tests/')
HEADER_ROOTS = SOURCE_ROOTS + ('include/psprecomp/',)


def relative_path(path):
    try:
        return Path(path).resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timeout', type=int, default=900, help='whole analysis deadline in seconds')
    args = parser.parse_args()
    tool = ROOT / 'out/cppcheck-tool/install/bin/cppcheck'
    version = subprocess.check_output([str(tool), '--version'], text=True).strip()
    if version != 'Cppcheck 2.22.0':
        parser.error('Cppcheck 2.22.0 is required')
    build = ROOT / 'out/cppcheck'
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
    entries = []
    for entry in database:
        path = (Path(entry['directory']) / entry['file']).resolve()
        relative = relative_path(path)
        if relative and relative.startswith(SOURCE_ROOTS):
            entries.append(entry)
    if not entries:
        parser.error('database has no first-party translation units')
    output = build / 'cppcheck-results'
    output.mkdir(parents=True, exist_ok=True)
    project = output / 'compile_commands.json'
    project.write_text(json.dumps(entries, indent=2) + '\n')
    command = [str(tool), f'--project={project}', '--std=c++20', '--platform=unix64',
               '--enable=warning', '--xml', '--xml-version=2', '--max-configs=1',
               '--check-level=normal', '-j2']
    started = time.monotonic()
    with subprocess.Popen(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          start_new_session=True) as process:
        try:
            stdout, stderr = process.communicate(timeout=args.timeout)
        except (subprocess.TimeoutExpired, KeyboardInterrupt):
            # Cppcheck's parallel executor may fork workers. Stop the owned
            # process group, rather than leaving work alive after the deadline.
            os.killpg(process.pid, signal.SIGKILL)
            stdout, stderr = process.communicate()
            (output / 'progress.log').write_text(stdout)
            (output / 'diagnostics.xml').write_text(stderr)
            (output / 'timeout.txt').write_text(f'Analysis interrupted or exceeded {args.timeout}s\n')
            return 1
        status = process.returncode
    (output / 'progress.log').write_text(stdout)
    (output / 'diagnostics.xml').write_text(stderr)
    # A malformed report or invocation failure must never produce a green job.
    if status != 0:
        print(stderr)
        return 1
    report = ET.fromstring(stderr)
    diagnostics = []
    for error in report.findall('errors/error'):
        locations = error.findall('location')
        primary = relative_path(locations[0].get('file')) if locations else None
        if locations and (not primary or not primary.startswith(HEADER_ROOTS)):
            continue
        diagnostics.append({'id': error.get('id'), 'severity': error.get('severity'),
                            'message': error.get('msg'), 'file': primary,
                            'line': locations[0].get('line') if locations else None})
    # Cppcheck 2.22.0 invalidates every iterator on vector::erase, but C++
    # preserves those before the erased element. Bound this reviewed exception
    # to the diagnostic, location and the entire reviewed free_block function;
    # a changed erase operation must never inherit this exception.
    exception = ('invalidContainer', 'profiles/mhp3rd/host/kernel/kernel.cpp', '832')
    context_sha256 = 'd3a5ee13614f241aebff59bbb76c834299f4a8cb545e09d6216a00f8c641c89b'
    waived = []
    failures = []
    for diagnostic in diagnostics:
        if diagnostic['severity'] != 'error':
            continue
        key = (diagnostic['id'], diagnostic['file'], diagnostic['line'])
        context_matches = False
        if key == exception:
            lines = (ROOT / key[1]).read_text().splitlines()
            context_matches = hashlib.sha256('\n'.join(lines[818:837]).encode()).hexdigest() == context_sha256
        if context_matches:
            waived.append(diagnostic)
        else:
            failures.append(diagnostic)

    summary = {'version': version, 'compile_commands': len(entries),
               'elapsed_seconds': round(time.monotonic() - started, 2),
               'diagnostics': diagnostics, 'reviewed_exceptions': waived, 'blocking_errors': failures}
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    visible = [d for d in diagnostics if d['severity'] != 'information']
    print(json.dumps({'compile_commands': len(entries),
                      'elapsed_seconds': summary['elapsed_seconds'],
                      'diagnostics': visible, 'reviewed_exceptions': waived,
                      'blocking_errors': failures}, indent=2), flush=True)
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
