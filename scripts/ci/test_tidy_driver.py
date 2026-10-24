"""First-party compilation database selection and bounded clang-tidy invocation."""

import contextlib
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import clang_tidy


@unittest.skipUnless(os.name == 'posix', 'Native clang-tidy executable fixtures use POSIX')
class TidyDriverTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.build = self.root / 'out/tidy'
        self.build.mkdir(parents=True)
        self.tool = self.root / 'out/tidy-tools/lib/python3.10/site-packages/clang_tidy/data/bin/clang-tidy'
        self.tool.parent.mkdir(parents=True)
        self.tool.write_text('#!' + sys.executable + '\n'
                             'import os, sys\n'
                             'if sys.argv[1:] == ["--version"]:\n'
                             '    print("LLVM version " + os.environ.get("TIDY_VERSION", "22.1.8"))\n'
                             'else:\n'
                             '    assert "-p" in sys.argv and "--config-file" in sys.argv\n'
                             '    assert "--quiet" in sys.argv\n'
                             '    print("public translation unit analyzed")\n'
                             '    sys.exit(1 if sys.argv[-1].endswith("bad.cpp") else 0)\n')
        self.tool.chmod(0o755)
        self.database = [{'directory': str(self.root), 'file': name, 'command': 'c++ -c ' + name}
                         for name in ('src/good.cpp', 'src/bad.cpp', 'vendor/private.cpp')]
        self.database.append({'directory': str(self.root.parent), 'file': 'outside.cpp', 'command': 'c++ -c outside.cpp'})

    def analyze(self, timeout=False, **environment):
        (self.build / 'compile_commands.json').write_text(json.dumps(self.database))
        real_run = subprocess.run

        def run(command, **kwargs):
            if command[0] == 'cmake':
                return subprocess.CompletedProcess(command, 0)
            if timeout and str(command[0]) == str(self.tool) and '--version' not in command:
                raise subprocess.TimeoutExpired(command, kwargs['timeout'])
            return real_run(command, **kwargs)

        with mock.patch.object(clang_tidy, 'ROOT', self.root), \
                mock.patch.dict(os.environ, environment), \
                mock.patch.object(sys, 'argv', ['clang_tidy.py', '--timeout', '2']), \
                mock.patch.object(clang_tidy.subprocess, 'run', side_effect=run), \
                contextlib.redirect_stdout(io.StringIO()):
            return clang_tidy.main()

    def test_owned_sources_are_deduplicated_and_all_failures_remain_visible(self):
        self.database.append(self.database[0])
        self.assertTrue(self.analyze())
        output = self.build / 'clang-tidy-results'
        scope = json.loads((output / 'coverage.json').read_text())
        self.assertEqual(scope['analyzed'], ['src/bad.cpp', 'src/good.cpp'])
        self.assertIn('vendor/private.cpp', scope['excluded'])
        self.assertTrue(any(name.endswith('outside.cpp') for name in scope['excluded']))
        summary = json.loads((output / 'summary.json').read_text())
        self.assertEqual(summary['translation_units'], 2)
        self.assertEqual(summary['failed'], ['src/bad.cpp'])
        self.assertIn('src/bad.cpp', (output / '001.log').read_text())

    def test_clean_source_succeeds_but_timeout_records_failure(self):
        self.database = self.database[:1]
        self.assertFalse(self.analyze())
        self.assertTrue(self.analyze(timeout=True))
        output = self.build / 'clang-tidy-results'
        self.assertIn('Timed out after 2s', (output / '001.log').read_text())
        self.assertEqual(json.loads((output / 'summary.json').read_text())['failed'], ['src/good.cpp'])

    def test_missing_tool_wrong_version_and_empty_scope_are_rejected(self):
        with self.assertRaises(SystemExit):
            self.analyze(TIDY_VERSION='21.1.8')
        self.database = self.database[2:]
        with self.assertRaises(SystemExit):
            self.analyze()
        self.tool.unlink()
        with self.assertRaises(SystemExit):
            self.analyze()


if __name__ == '__main__':
    unittest.main()
