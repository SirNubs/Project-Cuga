"""Formatter selection, refusal and mutation contracts using a real Git index."""

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

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import format_cpp


@unittest.skipUnless(os.name == 'posix', 'Synthetic formatter bridge uses POSIX executables')
class FormatDriverTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.git('init', '-q')
        self.git('config', 'user.name', 'Test Fixture')
        self.git('config', 'user.email', 'fixture@example.invalid')
        self.write('.clang-format', 'BasedOnStyle: LLVM\n')
        self.write('src/main.cpp', 'int value=1;\n')
        self.write('src/clean.hpp', 'int value = 1;\n')
        self.write('src/generated/private.cpp', 'int value=1;\n')
        self.write('docs/example.cpp', 'int value=1;\n')
        self.write('src/notes.txt', 'int value=1;\n')
        (self.root / 'src/linked.cpp').symlink_to('main.cpp')
        self.git('add', '.')
        self.git('commit', '-qm', 'Public fixture baseline')
        self.base = self.git('rev-parse', 'HEAD').stdout.strip()
        bridge = self.root / 'bridge'
        bridge.mkdir()
        formatter = bridge / 'clang-format'
        formatter.write_text('#!' + sys.executable + '\n'
                             'import os, sys\n'
                             'if sys.argv[1:] == ["--version"]:\n'
                             '    print("clang-format version " + os.environ.get("FORMAT_VERSION", "21.1.8"))\n'
                             'else:\n'
                             '    assert "--Werror" in sys.argv\n'
                             '    assert any(x.startswith("--style=file:") for x in sys.argv)\n'
                             '    assert any(x.startswith("--assume-filename=") for x in sys.argv)\n'
                             '    sys.stdout.buffer.write(sys.stdin.buffer.read().replace(b"value=1", b"value = 1"))\n')
        formatter.chmod(0o755)
        self.path = str(bridge) + os.pathsep + os.environ['PATH']

    def write(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)

    def git(self, *args):
        return subprocess.run(['git', '-C', str(self.root), *args], check=True,
                              capture_output=True, text=True, timeout=10)

    def run_driver(self, *args, version='21.1.8'):
        previous = Path.cwd()
        try:
            os.chdir(self.root)
            with mock.patch.dict(os.environ, PATH=self.path, FORMAT_VERSION=version), \
                    mock.patch.object(sys, 'argv', ['format_cpp.py', *args]), \
                    contextlib.redirect_stdout(io.StringIO()) as stdout, \
                    contextlib.redirect_stderr(io.StringIO()) as stderr:
                status = format_cpp.main()
            return status, stdout.getvalue(), stderr.getvalue()
        finally:
            os.chdir(previous)

    def test_scope_uses_index_modes_and_excludes_private_and_unrelated_files(self):
        self.assertEqual(format_cpp.tracked_sources(self.root), {'src/main.cpp', 'src/clean.hpp'})
        status, diff, diagnostic = self.run_driver('check', '--diff', '--json')
        self.assertEqual(status, 1)
        self.assertIn('--- a/src/main.cpp', diff)
        self.assertIn('+int value = 1;', diff)
        self.assertIn('2 checked, 1 differ', diagnostic)
        self.assertEqual((self.root / 'src/main.cpp').read_text(), 'int value=1;\n')
        summary = json.loads((self.root / 'out/format-review/baseline.json').read_text())
        self.assertEqual(summary['files'], ['src/main.cpp'])
        self.assertEqual(self.run_driver('report')[0], 0)

    def test_format_mutates_only_selected_tracked_sources(self):
        self.assertEqual(self.run_driver('format', 'src/main.cpp')[0], 0)
        self.assertEqual((self.root / 'src/main.cpp').read_text(), 'int value = 1;\n')
        self.assertEqual((self.root / 'src/generated/private.cpp').read_text(), 'int value=1;\n')
        self.assertEqual(self.run_driver('check')[0], 0)

    def test_diff_selection_uses_real_merge_base_and_handles_worktree_deletion(self):
        self.write('tests/new.cpp', 'int value=1;\n')
        self.git('add', '.')
        self.git('commit', '-qm', 'Add public test fixture')
        status, _, diagnostic = self.run_driver('check', '--base', self.base, '--json')
        self.assertEqual(status, 1)
        summary = json.loads((self.root / 'out/format-review/baseline.json').read_text())
        self.assertEqual(summary['files'], ['tests/new.cpp'])
        self.assertIn('1 checked', diagnostic)
        (self.root / 'tests/new.cpp').unlink()
        self.assertEqual(self.run_driver('check', '--base', self.base)[0], 0)
        self.assertEqual(self.run_driver('check', 'tests/new.cpp')[0], 2)

    def test_invalid_tool_scope_or_worktree_symlinks_are_rejected(self):
        self.assertEqual(self.run_driver('check', version='22.0.0')[0], 2)
        self.assertEqual(self.run_driver('format', 'docs/example.cpp')[0], 2)
        self.assertEqual(self.run_driver('check', '--samples')[0], 2)
        (self.root / 'src/main.cpp').unlink()
        (self.root / 'src/main.cpp').symlink_to('clean.hpp')
        status, _, error = self.run_driver('format', 'src/main.cpp')
        self.assertEqual(status, 2)
        self.assertIn('Refusing symlink path', error)
        self.assertEqual((self.root / 'src/clean.hpp').read_text(), 'int value = 1;\n')

    def test_all_six_samples_can_be_checked_without_widening_scope(self):
        for name in format_cpp.SAMPLES:
            self.write(name, 'int value = 1;\n')
        self.git('add', '.')
        status, _, diagnostic = self.run_driver('check', '--samples')
        self.assertEqual(status, 0)
        self.assertIn('6 checked', diagnostic)
        with self.assertRaises(SystemExit) as failure:
            self.run_driver('check', 'src/main.cpp', '--samples')
        self.assertEqual(failure.exception.code, 2)


if __name__ == '__main__':
    unittest.main()
