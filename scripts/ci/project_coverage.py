#!/usr/bin/env python3
"""Inventory owned source coverage and enforce coverage of changed lines."""

import argparse
import ast
import hashlib
import json
import os
import re
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOTS = ('include/psprecomp/', 'src/', 'tools/', 'scripts/',
                'profiles/mhp3rd/host/', 'profiles/mhp3rd/tools/', 'profiles/mhp3rd/packaging/',
                'profiles/mhp3rd/cmake/', 'profiles/mhp3rd/scripts/')
EXCLUDED = {'third_party', 'generated', 'game', 'overlays', 'overlay_corpora', 'out', 'build'}
KINDS = {'.c': 'native', '.cc': 'native', '.cpp': 'native', '.cxx': 'native',
         '.h': 'native', '.hh': 'native', '.hpp': 'native', '.hxx': 'native',
         '.py': 'python', '.java': 'java', '.kt': 'java',
         '.vert': 'shader', '.frag': 'shader', '.sh': 'build', '.cmake': 'build'}


def source_kind(name):
    path = Path(name)
    if name in {'CMakeLists.txt', 'Makefile', 'profiles/mhp3rd/CMakeLists.txt'} \
            or name.startswith('cmake/') and path.suffix == '.cmake':
        return 'build'
    if not name.startswith(SOURCE_ROOTS) or EXCLUDED.intersection(path.parts):
        return None
    if path.name.startswith('test_') or name.startswith('scripts/ci/tests/'):
        return None
    return 'build' if path.name == 'CMakeLists.txt' else KINDS.get(path.suffix)


def relative_name(name):
    path = Path(name)
    if not path.is_absolute():
        path = ROOT / path
    return path.resolve().relative_to(ROOT.resolve()).as_posix()


def read_lcov(text):
    result = {}
    current = None
    for line in text.splitlines():
        if line.startswith('SF:'):
            current = relative_name(line[3:])
            result.setdefault(current, {})
        elif line.startswith('DA:'):
            if current is None:
                raise ValueError('LCOV line record before source filename')
            number, count, *_ = line[3:].split(',')
            hits = result[current]
            hits[int(number)] = max(hits.get(int(number), 0), int(count))
        elif line == 'end_of_record':
            current = None
    return result


def read_python(data):
    return {relative_name(name): {line: int(line in file['executed_lines'])
                                 for line in file['executed_lines'] + file['missing_lines']}
            for name, file in data['files'].items()}


def read_java(text):
    result = {}
    for package in ET.fromstring(text).findall('package'):
        for file in package.findall('sourcefile'):
            name = 'profiles/mhp3rd/packaging/android/java/' + '/'.join(
                part for part in (package.get('name'), file.get('name')) if part)
            result[name] = {int(line.get('nr')): int(line.get('ci'))
                            for line in file.findall('line')}
    return result


def changed_lines(text):
    result = {}
    name = None
    for line in text.splitlines():
        if line.startswith('+++ '):
            value = line[4:]
            if value.startswith('"'):
                value = ast.literal_eval(value)
            name = value[2:] if value.startswith('b/') else None
        elif line.startswith('@@ '):
            match = re.match(r'@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@', line)
            if not match:
                raise ValueError('Malformed diff hunk')
            if name is not None:
                start = int(match[1])
                count = int(match[2]) if match[2] is not None else 1
                result.setdefault(name, set()).update(range(start, start + count))
    return result


def reviewed_unmapped_headers(root):
    """Recognize exact reviewed headers without fabricating executable lines.

    A source change invalidates its fingerprint and fails the normal mapping
    gate until reviewed again. Mapped lines always keep normal enforcement.
    """
    manifest = root / 'scripts/ci/coverage_unmapped_headers.json'
    if not manifest.is_file():
        return {}
    reviewed = json.loads(manifest.read_text())
    return {name: item['reason'] for name, item in reviewed.items()
            if source_kind(name) == 'native' and Path(name).suffix in {'.h', '.hh', '.hpp', '.hxx'}
            and item['reason'] and (root / name).is_file()
            and hashlib.sha256((root / name).read_bytes()).hexdigest() == item['sha256']}


def diff_coverage(changed, inventory, coverage, reviewed_headers=None):
    details = []
    covered = count = 0
    unmeasured = []
    reviewed = []
    for name, lines in sorted(changed.items()):
        if inventory.get(name) not in {'native', 'python', 'java'} or not lines:
            continue
        if name not in coverage:
            if name in (reviewed_headers or {}):
                reviewed.append({'file': name, 'reason': reviewed_headers[name]})
                continue
            unmeasured.append(name)
            continue
        hits = coverage[name]
        executable = lines & hits.keys()
        passed = {line for line in executable if hits[line] > 0}
        covered += len(passed)
        count += len(executable)
        details.append({'file': name, 'covered': len(passed), 'count': len(executable),
                        'missing_lines': sorted(executable - passed)})
    return {'covered': covered, 'count': count, 'percent': 100 * covered / count if count else None,
            'files': details, 'unmeasured_changed_files': unmeasured,
            'reviewed_unmapped_headers': reviewed}


def measured_totals(languages):
    covered = sum(value['covered'] for value in languages.values())
    count = sum(value['count'] for value in languages.values())
    return {'covered': covered, 'count': count,
            'percent': round(100 * covered / count, 2) if count else None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', default=os.environ.get('COVERAGE_BASE', 'origin/main'))
    parser.add_argument('--report-only', action='store_true')
    args = parser.parse_args()
    files = subprocess.check_output(['git', 'ls-files', '--stage', '-z'], cwd=ROOT).split(b'\0')
    inventory = {}
    for entry in files:
        if not entry:
            continue
        mode, name = entry.decode().split('\t', 1)
        kind = source_kind(name)
        if mode.split()[0] in {'100644', '100755'} and kind:
            inventory[name] = kind
    native = ROOT / 'out/coverage/coverage-results/coverage.info'
    python = ROOT / 'out/python-coverage/coverage.json'
    java = ROOT / 'out/java-coverage/report/jacoco.xml'
    coverage = read_lcov(native.read_text())
    coverage.update(read_python(json.loads(python.read_text())))
    if java.is_file():
        coverage.update(read_java(java.read_text()))
    coverage = {name: hits for name, hits in coverage.items() if name in inventory}
    base = args.base if args.base and set(args.base) != {'0'} else 'origin/main'
    base = subprocess.check_output(['git', 'rev-parse', '--verify', '--end-of-options',
                                    base + '^{commit}'], cwd=ROOT, text=True).strip()
    if not re.fullmatch(r'[0-9a-f]{40,64}', base):
        raise ValueError('Invalid base commit')
    merge_base = subprocess.check_output(['git', 'merge-base', base, 'HEAD'], cwd=ROOT, text=True).strip()
    diff = subprocess.check_output(['git', '-c', 'core.quotepath=false', 'diff', '--no-ext-diff',
                                    '--find-renames', '--unified=0', merge_base, 'HEAD', '--'],
                                   cwd=ROOT, text=True)
    changed = diff_coverage(changed_lines(diff), inventory, coverage, reviewed_unmapped_headers(ROOT))
    languages = {}
    for kind in sorted(set(inventory.values())):
        names = [name for name, value in inventory.items() if value == kind]
        mapped = [name for name in names if name in coverage]
        count = sum(len(coverage[name]) for name in mapped)
        covered = sum(sum(value > 0 for value in coverage[name].values()) for name in mapped)
        languages[kind] = {'files': len(names), 'mapped_files': len(mapped), 'covered': covered, 'count': count,
                           'percent': round(100 * covered / count, 2) if count else None,
                           'unmeasured_files': sorted(set(names) - set(mapped))}
    output = ROOT / 'out/project-coverage'
    output.mkdir(parents=True, exist_ok=True)
    overall = measured_totals(languages)
    summary = {'base': merge_base, 'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'],
               cwd=ROOT, text=True).strip(), 'languages': languages, 'diff': changed,
               'measured_totals': overall,
               'inventory': [{'file': name, 'kind': kind, 'mapped': name in coverage}
                             for name, kind in sorted(inventory.items())]}
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    lines = ['## Project coverage inventory', '', '| Scope | Mapped / owned files | Measured line coverage |',
             '| --- | --- | --- |']
    for kind, value in languages.items():
        percent = f'{value["percent"]:.2f}%' if value['percent'] is not None else 'Not measured'
        lines.append(f'| {kind} | {value["mapped_files"]} / {value["files"]} | {percent} |')
    percent = f'{changed["percent"]:.2f}%' if changed['percent'] is not None else 'N/A (no changed executable lines)'
    overall_percent = f'{overall["percent"]:.2f}%' if overall['percent'] is not None else 'Not measured'
    lines += ['', f'Combined measured source lines: **{overall_percent}**, '
              f'{overall["covered"]}/{overall["count"]}. No overall minimum; unknown lines are excluded.', '',
              f'Changed executable lines: **{percent}**, {changed["covered"]}/{changed["count"]}; '
              'required: **80%**.', '',
              'Unmeasured changed native/Python/Java files: ' + ', '.join(changed['unmeasured_changed_files']), '',
              'Reviewed headers without standalone line maps: ' +
              ', '.join(item['file'] for item in changed['reviewed_unmapped_headers']) +
              '. Exact source fingerprints are required; no coverage percentage is assigned.', '',
              'Unmeasured files stay visible in the inventory. Percentages apply to mapped lines, '
              'not unknown lines; this report does not claim complete project coverage. '
              'Shaders/build scripts require appropriate behavioral/build validation rather than a fabricated line metric.',
              '', 'Native inventory/diff counts use unique LCOV source lines. The separate LLVM native report '
              'retains its own line summaries; macro/template mappings can give different denominators.']
    markdown = '\n'.join(lines) + '\n'
    (output / 'summary.md').write_text(markdown)
    print(markdown)
    return int(not args.report_only and (bool(changed['unmeasured_changed_files']) or
               changed['percent'] is not None and changed['percent'] < 80))


if __name__ == '__main__':
    raise SystemExit(main())
