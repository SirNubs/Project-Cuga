"""Checksum and build-order contracts for the pinned installer."""

import contextlib
import hashlib
import io
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import install_cppcheck


class CppcheckInstallTests(unittest.TestCase):
    def install(self, checksum=True, failure=None):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = b'Public synthetic source archive'
            calls = []

            def run(command, **kwargs):
                self.assertTrue(kwargs['check'])
                calls.append(command)
                if command[0] == failure:
                    raise subprocess.CalledProcessError(1, command)
                if command[0] == 'curl':
                    Path(command[command.index('--output') + 1]).write_bytes(archive)
                return subprocess.CompletedProcess(command, 0)

            digest = hashlib.sha256(archive).hexdigest() if checksum else '0' * 64
            with mock.patch.object(install_cppcheck, '__file__', str(root / 'scripts/ci/install_cppcheck.py')), \
                    mock.patch.object(install_cppcheck, 'ARCHIVE_SHA256', digest), \
                    mock.patch.object(install_cppcheck.subprocess, 'run', side_effect=run), \
                    contextlib.redirect_stdout(io.StringIO()):
                try:
                    install_cppcheck.main()
                except (SystemExit, subprocess.CalledProcessError) as error:
                    return calls, error
            return calls, None

    def test_verified_source_is_built_without_gui_or_upstream_tests(self):
        calls, error = self.install()
        self.assertIsNone(error)
        self.assertEqual([Path(call[0]).name for call in calls],
                         ['curl', 'tar', 'cmake', 'cmake', 'cmake', 'cppcheck'])
        self.assertIn('--max-time', calls[0])
        self.assertIn(install_cppcheck.URL, calls[0])
        self.assertIn('-DBUILD_GUI=OFF', calls[2])
        self.assertIn('-DBUILD_TESTS=OFF', calls[2])
        self.assertIn('-j2', calls[3])
        self.assertEqual(calls[4][1], '--install')
        self.assertEqual(calls[5][1], '--version')

    def test_bad_checksum_never_extracts_or_builds(self):
        calls, error = self.install(checksum=False)
        self.assertIsInstance(error, SystemExit)
        self.assertIn('checksum mismatch', str(error))
        self.assertEqual(len(calls), 1)

    def test_download_and_configuration_failures_stop_publication(self):
        for tool, count in (('curl', 1), ('cmake', 3)):
            with self.subTest(tool=tool):
                calls, error = self.install(failure=tool)
                self.assertIsInstance(error, subprocess.CalledProcessError)
                self.assertEqual(len(calls), count)
                self.assertFalse(any('--install' in call for call in calls))


if __name__ == '__main__':
    unittest.main()
