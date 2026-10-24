#!/usr/bin/env python3
"""Run headless native tests and report first-party LLVM source coverage."""

import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'out/coverage'
ROOTS = ('include/psprecomp/', 'src/', 'tools/', 'profiles/mhp3rd/host/')
EXCLUDED = {'third_party', 'generated', 'game', 'overlays', 'overlay_corpora', 'out', 'build'}
SUFFIXES = {'.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx'}


def production_source(name):
    path = Path(name)
    return (name.startswith(ROOTS) and not EXCLUDED.intersection(path.parts)
            and path.suffix in SUFFIXES)


def tracked_sources():
    entries = subprocess.check_output(['git', 'ls-files', '--stage', '-z'], cwd=ROOT).split(b'\0')
    result = set()
    for entry in entries:
        if not entry:
            continue
        metadata, name = entry.decode().split('\t', 1)
        if metadata.split()[0] in {'100644', '100755'} and production_source(name):
            result.add(name)
    return result


def totals(files):
    result = {}
    for metric in ('lines', 'functions', 'branches'):
        count = sum(file['summary'][metric]['count'] for file in files)
        covered = sum(file['summary'][metric]['covered'] for file in files)
        result[metric] = {'covered': covered, 'count': count,
                          'percent': round(100 * covered / count, 2) if count else None}
    return result


def reviewed_zero_hash_stubs(diagnostics, emitted_names):
    """Accept only unused inline stubs whose emitted mapping is still present."""
    lines = diagnostics.strip().splitlines()
    if not lines:
        return []
    warning = re.fullmatch(r'warning: (\d+) functions have mismatched data', lines[0])
    names = []
    for line in lines[1:]:
        match = re.fullmatch(r"hash-mismatch: No profile record found for '(.*)' with hash = 0x0", line)
        if not match or match[1] not in emitted_names:
            raise ValueError('Unreviewed or missing LLVM coverage mapping: ' + line)
        names.append(match[1])
    if not warning or int(warning[1]) != len(names):
        raise ValueError('Unexpected LLVM coverage diagnostics')
    return names


def write_summary(markdown):
    """Keep report writes local; the workflow owns GitHub summary publication."""
    (BUILD / 'coverage-results' / 'summary.md').write_text(markdown)
    print(markdown)


def main():
    if sys.platform == 'darwin':
        cov, profdata = ['xcrun', 'llvm-cov'], ['xcrun', 'llvm-profdata']
    elif sys.platform.startswith('linux'):
        cov, profdata = ['llvm-cov-18'], ['llvm-profdata-18']
    else:
        raise SystemExit('Coverage requires native macOS Clang or Linux Clang 18')
    for command in (cov, profdata):
        subprocess.run(command + ['--version'], check=True, timeout=30)
    cache = (BUILD / 'CMakeCache.txt').read_text()
    if 'PSPRECOMP_COVERAGE:BOOL=ON' not in cache:
        raise SystemExit('Configure out/coverage with PSPRECOMP_COVERAGE=ON first')
    # Discover the registered executables, including tests with zero hits.
    database = json.loads(subprocess.check_output(
        ['ctest', '--test-dir', str(BUILD), '--show-only=json-v1'], text=True))
    objects = set()
    for test in database['tests']:
        binary = Path(test['command'][0]).resolve()
        binary.relative_to(BUILD.resolve())
        if not binary.is_file():
            raise SystemExit(f'Missing native test executable: {binary.name}')
        objects.add(str(binary))
    if not objects:
        raise SystemExit('No registered native tests')
    # Remove only this runner's previous outputs to avoid stale-profile inflation.
    output = BUILD / 'coverage-results'
    if output.is_symlink():
        raise SystemExit('Coverage output must not be a symlink')
    if output.exists():
        shutil.rmtree(output)
    raw = output / 'raw'
    raw.mkdir(parents=True)
    environment = dict(os.environ, LLVM_PROFILE_FILE=str(raw / '%m-%p.profraw'))
    subprocess.run(['ctest', '--test-dir', str(BUILD), '--output-on-failure',
                    '--no-tests=error', '--timeout', '120', '--output-junit',
                    str(output / 'test-results.xml')], env=environment, check=True)
    # Exercise public CLI contracts and flush zero counters for the complete
    # application/tool mappings. No game data or interactive window is needed.
    cli_cases = [([str(BUILD / 'bin/Yakumo'), '--help'], 0),
                 ([str(BUILD / 'bin/Yakumo'), '--not-a-yakumo-option'], 2),
                 ([str(BUILD / 'psp_analyze')], 2),
                 ([str(BUILD / 'psp_recomp')], 2),
                 ([str(BUILD / 'dump_function')], 2)]
    for command, expected in cli_cases:
        result = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=10)
        if result.returncode != expected or 'usage:' not in (result.stdout + result.stderr).lower():
            raise RuntimeError(f'CLI contract failed: {Path(command[0]).name}: {result.returncode}')
        objects.add(command[0])
    profiles = sorted(str(path) for path in raw.glob('*.profraw'))
    if not profiles:
        raise SystemExit('No raw profiles; coverage instrumentation did not run')
    merged = output / 'tests.profdata'
    # Keep zero-count records for code compiled into the application but not
    # yet exercised; sparse merging can lose alternative same-name hashes.
    subprocess.run(profdata + ['merge', *profiles, '-o', str(merged)], check=True)
    binaries = sorted(objects)
    arguments = [binaries[0]]
    for binary in binaries[1:]:
        arguments += ['-object', binary]
    # LLVM 18's parallel summary/debug export has aborted inside its thread
    # pool with an invalid free in CI. Serialize reporting without dropping
    # binaries, mappings or the strict mismatch validation.
    arguments += [f'-instr-profile={merged}', '-num-threads=1']
    result = subprocess.run(cov + ['export', *arguments], capture_output=True, text=True, check=True)
    (output / 'export-diagnostics.txt').write_text(result.stderr)
    export = json.loads(result.stdout)
    stubs = []
    if result.stderr:
        debug = subprocess.run(cov + ['export', *arguments, '-summary-only', '-dump'],
                               capture_output=True, text=True)
        (output / 'llvm-diagnostics.txt').write_text(debug.stderr)
        debug.check_returncode()
        emitted = {function['name'] for function in export['data'][0]['functions'] if function['regions']}
        stubs = reviewed_zero_hash_stubs(debug.stderr, emitted)
    tracked = tracked_sources()
    files = []
    sources = []
    for file in export['data'][0]['files']:
        path = Path(file['filename']).resolve()
        try:
            name = path.relative_to(ROOT.resolve()).as_posix()
        except ValueError:
            continue
        if name in tracked:
            files.append({'filename': name, 'summary': file['summary']})
            sources.append(str(path))
    if not files or not totals(files)['lines']['count']:
        raise SystemExit('No first-party executable lines in the coverage report')
    files.sort(key=lambda file: file['filename'])
    summary = {'scope': 'First-party production C++ represented in public application, tools and native tests',
               'tests': len(database['tests']), 'files': files, 'totals': totals(files),
               'reviewed_zero_hash_stub_mappings': stubs,
               'unrepresented_first_party_files': sorted(tracked - {file['filename'] for file in files})}
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    with (output / 'coverage.info').open('w') as stream:
        subprocess.run(cov + ['export', *arguments, *sources, '-format=lcov'],
                       stdout=stream, check=True)
    with (output / 'report.txt').open('w') as stream:
        report = subprocess.run(cov + ['report', *arguments, *sources], stdout=stream,
                                stderr=subprocess.PIPE, text=True, check=True)
    html = subprocess.run(cov + ['show', *arguments, *sources, '-format=html',
                               f'-output-dir={output / "html"}', '-show-line-counts-or-regions'],
                          capture_output=True, text=True, check=True)
    (output / 'report-diagnostics.txt').write_text(report.stderr + html.stderr)
    text = ['## Native C++ coverage', '', '| Metric | Covered / total | Coverage |',
            '| --- | --- | --- |']
    for name, value in summary['totals'].items():
        percent = f'{value["percent"]:.2f}%' if value['percent'] is not None else 'N/A'
        text.append(f'| {name.title()} | {value["covered"]} / {value["count"]} | {percent} |')
    text += ['', f'{len(files)} first-party production files represented across {summary["tests"]} native tests.',
             f'{len(summary["unrepresented_first_party_files"])} tracked production files have no coverage mapping; '
             'they are listed separately and are not included in these percentages.', '',
             'Scope excludes test code, vendor/generated/game code and Python. Headless coverage does not '
             'measure full gameplay, renderer/audio/platform paths absent from these binaries, or child paths '
             'that abort before flushing a profile. This is not a whole-project coverage percentage.']
    if stubs:
        text += ['', f'LLVM reported {len(stubs)} zero-hash unused inline stub mappings; '
                 'each function also has an emitted mapping in the exported data. '
                 'Diagnostics are retained. Any nonzero mismatch or missing emitted mapping fails the report.']
    markdown = '\n'.join(text) + '\n'
    write_summary(markdown)


if __name__ == '__main__':
    main()
