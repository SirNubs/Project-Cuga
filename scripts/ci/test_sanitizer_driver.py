"""Sanitizer gate and diagnostic contracts, independent of a platform compiler."""

import contextlib
import io
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import check_sanitizers


class SanitizerDriverTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.build = self.root / 'build'
        self.build.mkdir()
        (self.build / 'CMakeCache.txt').write_text(
            '// Public fixture\n# Ignore comments\nPSPRECOMP_SANITIZERS:BOOL=ON\n'
            f'CMAKE_HOME_DIRECTORY:INTERNAL={self.root}\nCMAKE_CXX_COMPILER:FILEPATH=clang++\n')
        self.command = 'clang++ -fsanitize=address,undefined -fno-sanitize-recover=all -c public.cpp'
        self.calls = []

    def run_probes(self, fault=None):
        (self.build / 'compile_commands.json').write_text(json.dumps([{'file': 'public.cpp', 'command': self.command}]))
        reports = {'address': 'AddressSanitizer: heap-use-after-free',
                   'undefined': 'runtime error: signed integer overflow',
                   'leak': 'LeakSanitizer: detected memory leaks'}

        def run(command, **kwargs):
            self.calls.append(command)
            if command[0] == 'cmake':
                self.assertEqual(kwargs['timeout'], 120)
                if '-S' in command:
                    source = Path(command[command.index('-S') + 1])
                    self.assertIn('SANITIZER_MODULE', (source / 'CMakeLists.txt').read_text())
                    for name in reports:
                        self.assertTrue((source / f'{name}.cpp').is_file())
                return subprocess.CompletedProcess(command, 1 if fault == 'build' else 0,
                                                   'configuration output', 'configuration diagnostics')
            self.assertEqual(kwargs['timeout'], 30)
            name = Path(command[0]).name
            return subprocess.CompletedProcess(command, 0 if fault == 'success' else 1, '',
                                               'unrelated failure' if fault == 'wrong-report' else reports[name])

        with mock.patch.object(sys, 'argv', ['check_sanitizers.py', str(self.build)]), \
                mock.patch.object(check_sanitizers.subprocess, 'run', side_effect=run), \
                contextlib.redirect_stdout(io.StringIO()):
            return check_sanitizers.main()

    def test_all_three_probes_require_their_own_failure_diagnostics(self):
        self.assertEqual(self.run_probes(), 0)
        self.assertEqual(len(self.calls), 5)
        self.assertIn('-j2', self.calls[1])
        diagnostics = self.build / 'sanitizer-probes'
        for name in ('address', 'undefined', 'leak'):
            self.assertTrue((diagnostics / f'{name}.log').is_file())
        self.assertEqual(list(self.build.glob('sanitizer-probes-*')), [], 'Disposable source project must be removed')

    def test_success_exit_or_unrelated_failure_cannot_prove_instrumentation(self):
        for fault in ('success', 'wrong-report'):
            with self.subTest(fault=fault), self.assertRaisesRegex(RuntimeError, 'expected sanitizer report'):
                self.run_probes(fault)

    def test_configuration_failure_keeps_diagnostics_and_stops_before_probes(self):
        with self.assertRaises(subprocess.CalledProcessError):
            self.run_probes('build')
        self.assertEqual(len(self.calls), 1)
        self.assertIn('configuration diagnostics', (self.build / 'sanitizer-probes/build-0.log').read_text())
        self.assertEqual(list(self.build.glob('sanitizer-probes-*')), [])

    def test_disabled_or_incomplete_compile_instrumentation_is_rejected(self):
        cache = self.build / 'CMakeCache.txt'
        original = cache.read_text()
        cache.write_text(original.replace('BOOL=ON', 'BOOL=OFF'))
        with self.assertRaisesRegex(RuntimeError, 'must enable'):
            self.run_probes()
        cache.write_text(original)
        for flag in ('-fsanitize=address,undefined', '-fno-sanitize-recover=all'):
            self.command = 'clang++ -fsanitize=address,undefined -fno-sanitize-recover=all -c public.cpp'.replace(flag, '')
            with self.subTest(flag=flag), self.assertRaisesRegex(RuntimeError, 'Missing sanitizer compile'):
                self.run_probes()
        self.assertEqual(self.calls, [])


if __name__ == '__main__':
    unittest.main()
