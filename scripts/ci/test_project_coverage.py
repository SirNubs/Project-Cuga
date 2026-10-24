"""Exercise coverage formats, ownership and the gate against a real Git diff."""

import contextlib
import io
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import project_coverage as report


class CoverageFormatsTests(unittest.TestCase):
    def test_inventory_owns_sources_but_not_vendor_or_test_code(self):
        cases = {'src/runtime.cpp': 'native', 'scripts/format_cpp.py': 'python',
                 'profiles/mhp3rd/packaging/android/java/App.java': 'java',
                 'profiles/mhp3rd/host/ge/shader.vert': 'shader',
                 'profiles/mhp3rd/packaging/linux/build.sh': 'build',
                 'CMakeLists.txt': 'build', 'cmake/Coverage.cmake': 'build',
                 'Makefile': 'build', 'profiles/mhp3rd/CMakeLists.txt': 'build',
                 'profiles/mhp3rd/cmake/FFmpeg.cmake': 'build',
                 'profiles/mhp3rd/scripts/release_linux.sh': 'build',
                 'tests/main.cpp': None, 'scripts/ci/test_project_coverage.py': None,
                 'scripts/ci/tests/fixture.py': None,
                 'profiles/mhp3rd/host/third_party/imgui/imgui.cpp': None,
                 'profiles/mhp3rd/generated/function.cpp': None}
        for name, kind in cases.items():
            with self.subTest(name=name):
                self.assertEqual(report.source_kind(name), kind)

    def test_lcov_merges_duplicate_records_without_losing_hits(self):
        data = 'SF:src/runtime.cpp\nDA:1,0\nDA:2,3,checksum\nend_of_record\n'
        data += 'SF:src/runtime.cpp\nDA:1,1\nDA:2,0\nend_of_record\n'
        self.assertEqual(report.read_lcov(data), {'src/runtime.cpp': {1: 1, 2: 3}})
        with self.assertRaises(ValueError):
            report.read_lcov('DA:1,2\n')
        with self.assertRaises(ValueError):
            report.read_lcov('SF:src/runtime.cpp\nDA:invalid,2\n')
        with self.assertRaises(ValueError):
            report.relative_name('/outside/owned/source.cpp')

    def test_python_and_java_map_executable_lines(self):
        python = {'files': {'scripts/format_cpp.py': {'executed_lines': [1, 2], 'missing_lines': [4]}}}
        self.assertEqual(report.read_python(python), {'scripts/format_cpp.py': {1: 1, 2: 1, 4: 0}})
        java = '<report><package name="io/example"><sourcefile name="App.java">'
        java += '<line nr="4" ci="0"/><line nr="8" ci="2"/></sourcefile></package></report>'
        self.assertEqual(report.read_java(java), {
            'profiles/mhp3rd/packaging/android/java/io/example/App.java': {4: 0, 8: 2}})

    def test_diff_hunks_handle_insertions_deletions_and_quoted_names(self):
        diff = '+++ b/src/runtime.cpp\n@@ -1 +2,3 @@\n@@ -10,3 +12,0 @@\n'
        diff += '+++ "b/scripts/a b.py"\n@@ -0,0 +1 @@\n+++ /dev/null\n@@ -1 +0,0 @@\n'
        self.assertEqual(report.changed_lines(diff), {'src/runtime.cpp': {2, 3, 4}, 'scripts/a b.py': {1}})
        with self.assertRaises(ValueError):
            report.changed_lines('+++ b/src/runtime.cpp\n@@ malformed @@\n')

    def test_weighted_diff_excludes_comments_and_reports_missing_mapping(self):
        changed = {'src/a.cpp': {1, 2, 3}, 'scripts/b.py': {1, 2, 3, 4},
                   'profiles/mhp3rd/packaging/android/java/App.java': {1},
                   'docs/a.md': {1}, 'src/deleted.cpp': set()}
        inventory = {'src/a.cpp': 'native', 'scripts/b.py': 'python',
                     'profiles/mhp3rd/packaging/android/java/App.java': 'java',
                     'src/deleted.cpp': 'native'}
        result = report.diff_coverage(changed, inventory,
                                      {'src/a.cpp': {1: 9, 2: 0}, 'scripts/b.py': {1: 1, 2: 1, 3: 1, 4: 1}})
        self.assertEqual((result['covered'], result['count']), (5, 6))
        self.assertAlmostEqual(result['percent'], 100 * 5 / 6)
        self.assertEqual(result['files'][0]['missing_lines'], [])
        self.assertEqual(result['files'][1]['missing_lines'], [2])
        self.assertEqual(result['unmeasured_changed_files'],
                         ['profiles/mhp3rd/packaging/android/java/App.java'])
        self.assertIsNone(report.diff_coverage({}, {}, {})['percent'])

    def test_overall_is_weighted_and_unknown_counts_are_not_invented(self):
        value = report.measured_totals({'native': {'covered': 100, 'count': 200},
                                        'python': {'covered': 5, 'count': 5},
                                        'shader': {'covered': 0, 'count': 0}})
        self.assertEqual(value, {'covered': 105, 'count': 205, 'percent': 51.22})
        self.assertIsNone(report.measured_totals({})['percent'])


class CoverageGateIntegrationTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.git('init', '-q')
        self.git('config', 'user.name', 'Coverage tests')
        self.git('config', 'user.email', 'coverage@example.invalid')
        self.write('src/runtime.cpp', 'old\n')
        self.write('scripts/fixture.py', 'old\n')
        self.write('profiles/mhp3rd/packaging/android/java/App.java', 'old\n')
        self.write('profiles/mhp3rd/host/ge/shader.frag', 'old\n')
        self.git('add', '.')
        self.git('commit', '-qm', 'Base')
        self.base = self.git('rev-parse', 'HEAD').strip()
        self.git('update-ref', 'refs/remotes/origin/main', self.base)
        self.write('src/runtime.cpp', 'one\ntwo\nthree\nfour\nfive\n')
        self.git('add', '.')
        self.git('commit', '-qm', 'Change public source')
        self.write('out/coverage/coverage-results/coverage.info',
                   'SF:src/runtime.cpp\n' + ''.join(f'DA:{n},{int(n <= 4)}\n' for n in range(1, 6)) +
                   'end_of_record\nSF:tests/unowned.cpp\nDA:1,1\nend_of_record\n')
        self.write('out/python-coverage/coverage.json', json.dumps({'files': {
            'scripts/fixture.py': {'executed_lines': [1], 'missing_lines': [2]}}}))

    def git(self, *arguments):
        return subprocess.check_output(['git', *arguments], cwd=self.root, text=True, stderr=subprocess.PIPE)

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def run_gate(self, *arguments):
        with patch.object(report, 'ROOT', self.root), patch.object(sys, 'argv', ['project_coverage', *arguments]), \
                patch.dict(os.environ, {'COVERAGE_BASE': self.base}), \
                contextlib.redirect_stdout(io.StringIO()):
            status = report.main()
        summary = json.loads((self.root / 'out/project-coverage/summary.json').read_text())
        return status, summary

    def test_exactly_eighty_percent_passes_and_reports_unknown_scopes(self):
        status, summary = self.run_gate()
        self.assertEqual(status, 0)
        self.assertEqual(summary['base'], self.base)
        self.assertEqual(summary['diff']['percent'], 80)
        self.assertEqual(summary['languages']['python']['percent'], 50)
        self.assertEqual(summary['languages']['native']['count'], 5)
        self.assertEqual(summary['measured_totals'], {'covered': 5, 'count': 7, 'percent': 71.43})
        self.assertIsNone(summary['languages']['java']['percent'])
        self.assertIsNone(summary['languages']['shader']['percent'])
        self.assertIn('80.00%', (self.root / 'out/project-coverage/summary.md').read_text())

    def test_below_threshold_fails_but_report_only_does_not_enforce(self):
        path = self.root / 'out/coverage/coverage-results/coverage.info'
        path.write_text(path.read_text().replace('DA:4,1', 'DA:4,0'))
        self.assertEqual(self.run_gate()[0], 1)
        status, summary = self.run_gate('--report-only')
        self.assertEqual(status, 0)
        self.assertEqual(summary['diff']['percent'], 60)

    def test_reviewed_header_stays_unmeasured_and_source_mutation_fails(self):
        name = 'profiles/mhp3rd/host/ui/example.hpp'
        self.write(name, '#pragma once\nvoid example();\n')
        fingerprint = hashlib.sha256((self.root / name).read_bytes()).hexdigest()
        self.write('scripts/ci/coverage_unmapped_headers.json', json.dumps({name: {
            'sha256': fingerprint, 'reason': 'Only a function declaration.'}}))
        self.git('add', name)
        self.git('commit', '-qm', 'Add declaration')
        status, summary = self.run_gate()
        self.assertEqual(status, 0)
        self.assertFalse(summary['diff']['unmeasured_changed_files'])
        self.assertEqual(summary['diff']['reviewed_unmapped_headers'][0]['file'], name)
        self.assertIn(name, summary['languages']['native']['unmeasured_files'])
        self.assertEqual(summary['languages']['native']['count'], 5)
        self.write(name, '#pragma once\ninline int example() { return 42; }\n')
        self.git('add', name)
        self.git('commit', '-qm', 'Add an unmapped function body')
        status, summary = self.run_gate()
        self.assertEqual(status, 1)
        self.assertEqual(summary['diff']['unmeasured_changed_files'], [name])
        self.assertFalse(summary['diff']['reviewed_unmapped_headers'])

    def test_reviewed_header_cannot_bypass_mapped_line_threshold(self):
        name = 'profiles/mhp3rd/host/ui/example.hpp'
        result = report.diff_coverage({name: {1}}, {name: 'native'}, {name: {1: 0}},
                                     {name: 'Reviewed declaration'})
        self.assertEqual(result['percent'], 0)
        self.assertFalse(result['reviewed_unmapped_headers'])

    def test_changed_java_requires_mapping_and_can_be_covered(self):
        self.write('profiles/mhp3rd/packaging/android/java/App.java', 'new\n')
        self.git('add', 'profiles/mhp3rd/packaging/android/java/App.java')
        self.git('commit', '-qm', 'Change Java')
        self.assertEqual(self.run_gate()[0], 1)
        self.write('out/java-coverage/report/jacoco.xml', '<report><package name=""><sourcefile name="App.java">'
                   '<line nr="1" ci="1"/></sourcefile></package></report>')
        self.assertEqual(self.run_gate()[0], 0)

    def test_zero_push_base_uses_main_and_no_executable_diff_is_available(self):
        self.assertEqual(self.run_gate('--base', '0' * 40)[0], 0)
        status, summary = self.run_gate('--base', 'HEAD')
        self.assertEqual(status, 0)
        self.assertIsNone(summary['diff']['percent'])


if __name__ == '__main__':
    unittest.main()
