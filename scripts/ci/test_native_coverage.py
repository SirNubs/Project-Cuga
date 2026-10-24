"""Guard the coverage denominator and first-party ownership policy."""

import contextlib
import io
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import native_coverage
from native_coverage import production_source, reviewed_zero_hash_stubs, totals, write_summary


class CoveragePolicyTests(unittest.TestCase):
    def test_summary_does_not_write_to_environment_selected_path(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'coverage-results').mkdir()
            unrelated = root / 'unrelated.txt'
            unrelated.write_text('Keep this file unchanged.\n')
            with mock.patch.object(native_coverage, 'BUILD', root), \
                    mock.patch.dict(os.environ, {'GITHUB_STEP_SUMMARY': str(unrelated)}), \
                    contextlib.redirect_stdout(io.StringIO()) as stream:
                write_summary('Measured coverage\n')
            self.assertEqual(unrelated.read_text(), 'Keep this file unchanged.\n')
            self.assertEqual((root / 'coverage-results/summary.md').read_text(), 'Measured coverage\n')
            self.assertIn('Measured coverage', stream.getvalue())

    def test_only_production_sources(self):
        for path in ('src/runtime.cpp', 'include/psprecomp/guest_memory.hpp',
                     'profiles/mhp3rd/host/kernel/kernel.cpp'):
            self.assertTrue(production_source(path), path)
        for path in ('tests/test_main.cpp', 'profiles/mhp3rd/tests/input_tests.cpp',
                     'profiles/mhp3rd/generated/generated.cpp',
                     'profiles/mhp3rd/host/third_party/imgui/imgui.cpp',
                     'src/generated/example.cpp', 'scripts/ci/native_coverage.py',
                     '/tmp/src/runtime.cpp', 'src/runtime.cpp.bak'):
            self.assertFalse(production_source(path), path)

    def test_weighted_counts_not_average_of_percentages(self):
        files = [{'summary': {'lines': {'count': 10, 'covered': 8},
                              'functions': {'count': 2, 'covered': 2},
                              'branches': {'count': 0, 'covered': 0}}},
                 {'summary': {'lines': {'count': 90, 'covered': 0},
                              'functions': {'count': 8, 'covered': 0},
                              'branches': {'count': 0, 'covered': 0}}}]
        result = totals(files)
        self.assertEqual(result['lines'], {'count': 100, 'covered': 8, 'percent': 8.0})
        self.assertEqual(result['functions']['percent'], 20.0)
        self.assertIsNone(result['branches']['percent'])

    def test_empty_counts_are_unavailable_not_fully_covered(self):
        self.assertIsNone(totals([])['lines']['percent'])

    def test_only_duplicate_zero_hash_stubs_are_reviewed(self):
        diagnostic = "warning: 1 functions have mismatched data\nhash-mismatch: No profile record found for 'inline' with hash = 0x0\n"
        self.assertEqual(reviewed_zero_hash_stubs(diagnostic, {'inline'}), ['inline'])
        with self.assertRaises(ValueError):
            reviewed_zero_hash_stubs(diagnostic, set())
        with self.assertRaises(ValueError):
            reviewed_zero_hash_stubs(diagnostic.replace('hash = 0x0', 'hash = 0x1'), {'inline'})
        with self.assertRaises(ValueError):
            reviewed_zero_hash_stubs('warning: profile data may be out of date', {'inline'})


if __name__ == '__main__':
    unittest.main()
