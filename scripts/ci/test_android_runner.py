"""Public Android runner contracts through synthetic CTest and ADB executables."""

import contextlib
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path
from unittest import mock

import android_tests


@unittest.skipUnless(os.name == 'posix', 'Synthetic CTest/ADB bridge uses POSIX executables')
class AndroidRunnerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / 'build with spaces'
        self.build.mkdir()
        self.stl = self.root / 'libc++_shared.so'
        self.stl.write_bytes(b'synthetic library')
        self.bridge = self.root / 'bridge'
        self.bridge.mkdir()
        self.calls = self.root / 'calls.jsonl'
        self.database = self.root / 'tests.json'
        self.tests = []
        for name in ('passing_tests', 'failing_tests'):
            (self.build / name).write_bytes(b'public executable fixture')
            self.tests.append({'name': name, 'command': [str(self.build / name), 'arg with spaces', "quote's"]})
        (self.build / 'psp_recomp').write_bytes(b'public codegen fixture')
        executable = ('#!' + sys.executable + '\n'
                      'import json, os, sys\nfrom pathlib import Path\n'
                      'if Path(sys.argv[0]).name == "ctest":\n'
                      '    print(Path(os.environ["CTEST_DATABASE"]).read_text())\n'
                      'else:\n'
                      '    with open(os.environ["ADB_CALLS"], "a") as log:\n'
                      '        log.write(json.dumps(sys.argv[1:]) + "\\n")\n'
                      '    if sys.argv[1] == "shell" and "timeout 120" in sys.argv[2]:\n'
                      '        print("Synthetic remote result")\n'
                      '        sys.exit(7 if "failing_tests" in sys.argv[2] else 0)\n')
        for name in ('ctest', 'adb'):
            script = self.bridge / name
            script.write_text(executable)
            script.chmod(0o755)

    def run_suite(self, tests, timeout=False):
        self.database.write_text(json.dumps({'tests': tests}))
        environment = {'PATH': str(self.bridge) + os.pathsep + os.environ['PATH'],
                       'CTEST_DATABASE': str(self.database), 'ADB_CALLS': str(self.calls)}
        real_run = subprocess.run

        def run(command, **kwargs):
            if timeout and command[0] == 'adb' and command[1] == 'shell' and 'timeout 120' in command[2]:
                raise subprocess.TimeoutExpired(command, kwargs['timeout'])
            return real_run(command, **kwargs)

        with mock.patch.dict(os.environ, environment), \
                mock.patch.object(sys, 'argv', ['android_tests.py', str(self.build), str(self.stl)]), \
                mock.patch.object(android_tests.subprocess, 'run', side_effect=run), \
                contextlib.redirect_stdout(io.StringIO()):
            return android_tests.main()

    def test_all_registered_tests_run_and_failure_is_retained(self):
        self.assertEqual(self.run_suite(self.tests), 1)
        suite = ET.parse(self.build / 'android-results/test-results.xml').getroot()
        self.assertEqual(suite.attrib['tests'], '2')
        self.assertEqual(suite.attrib['failures'], '1')
        cases = suite.findall('testcase')
        self.assertIsNone(cases[0].find('failure'))
        self.assertEqual(cases[1].find('failure').attrib['message'], 'Exit status 7')
        self.assertIn('Synthetic remote result', cases[1].find('system-out').text)
        calls = [json.loads(line) for line in self.calls.read_text().splitlines()]
        commands = [call[1] for call in calls if call[0] == 'shell' and 'timeout 120' in call[1]]
        self.assertEqual(len(commands), 2)
        self.assertIn("'arg with spaces'", commands[0])
        self.assertIn("'quote'\"'\"'s'", commands[0])
        self.assertEqual(calls[-1], ['shell', 'rm -rf /data/local/tmp/yakumo-unit-tests'])
        self.assertEqual(len([call for call in calls if call[0] == 'push']), 5)
        self.assertIn('PSPRECOMP_TEST_NIDS_CSV=/data/local/tmp/yakumo-unit-tests/nids.csv', commands[0])
        self.assertTrue(any(call[0] == 'push' and call[-1].endswith('/nids.csv') for call in calls))

    def test_success_and_bridge_timeout_have_different_statuses(self):
        self.assertEqual(self.run_suite(self.tests[:1]), 0)
        self.assertEqual(self.run_suite(self.tests[:1], timeout=True), 1)
        case = ET.parse(self.build / 'android-results/test-results.xml').find('testcase')
        self.assertEqual(case.find('failure').attrib['message'], 'Exit status 124')
        self.assertIn('135 seconds', case.find('system-out').text)
        self.assertIn('rm -rf /data/local/tmp/yakumo-unit-tests', self.calls.read_text().splitlines()[-1])

    def test_empty_or_unsafe_registration_fails_before_any_adb_call(self):
        cases = [[], [self.tests[0], self.tests[0]],
                 [{'name': '../unsafe', 'command': self.tests[0]['command']}],
                 [{'name': 'other_tests', 'command': self.tests[0]['command'],
                   'properties': [{'name': 'ENVIRONMENT', 'value': ['bad-name=value']}]}]]
        outside = self.root / 'outside_tests'
        outside.write_bytes(b'outside build')
        cases.append([{'name': 'outside_tests', 'command': [str(outside)]}])
        (self.build / 'link_tests').symlink_to(outside)
        cases.append([{'name': 'link_tests', 'command': [str(self.build / 'link_tests')]}])
        for tests in cases:
            with self.subTest(tests=tests), self.assertRaises((ValueError, RuntimeError)):
                self.run_suite(tests)
            self.assertFalse(self.calls.exists(), 'Invalid input must not touch the device')

    def test_shared_binary_variant_preserves_arguments_and_environment(self):
        variant = {'name': 'memory_watch_contracts',
                   'command': [str(self.build / 'passing_tests'), '--memory-watch'],
                   'properties': [{'name': 'ENVIRONMENT',
                                   'value': ['PSPRECOMP_WATCH_WRITE=0x08804000',
                                             "FIXTURE_VALUE=space and 'quote'"]}]}
        self.assertEqual(self.run_suite([variant]), 0)
        calls = [json.loads(line) for line in self.calls.read_text().splitlines()]
        command = next(call[1] for call in calls if call[0] == 'shell' and 'timeout 120' in call[1])
        self.assertIn('env PSPRECOMP_WATCH_WRITE=0x08804000', command)
        self.assertIn('/passing_tests --memory-watch', command)
        self.assertIn("'FIXTURE_VALUE=space and '\"'\"'quote'\"'\"''", command)
        self.assertEqual(ET.parse(self.build / 'android-results/test-results.xml')
                         .find('testcase').attrib['name'], 'memory_watch_contracts')

    def test_runtime_library_must_have_the_ndk_name(self):
        wrong = self.root / 'unexpected.so'
        wrong.write_bytes(b'wrong library')
        self.stl = wrong
        with self.assertRaises(ValueError):
            self.run_suite(self.tests)
        self.assertFalse(self.calls.exists())


if __name__ == '__main__':
    unittest.main()
