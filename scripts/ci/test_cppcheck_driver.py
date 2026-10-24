"""Analyzer report ownership, blocking errors and bounded process contracts."""

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

import cppcheck


@unittest.skipUnless(os.name == 'posix', 'Cppcheck process-group runner is Linux/POSIX')
class CppcheckDriverTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.build = self.root / 'out/cppcheck'
        self.build.mkdir(parents=True)
        self.database = [self.entry('src/public.cpp'), self.entry('vendor/private.cpp'),
                         {'directory': str(self.root.parent), 'file': 'outside.cpp', 'command': 'c++ -c outside.cpp'}]
        self.report = self.root / 'report.xml'
        tool = self.root / 'out/cppcheck-tool/install/bin/cppcheck'
        tool.parent.mkdir(parents=True)
        tool.write_text('#!' + sys.executable + '\n'
                        'import json, os, sys, time\nfrom pathlib import Path\n'
                        'if sys.argv[1:] == ["--version"]:\n'
                        '    print(os.environ.get("TOOL_VERSION", "Cppcheck 2.22.0"))\n'
                        'else:\n'
                        '    project = next(a.split("=", 1)[1] for a in sys.argv if a.startswith("--project="))\n'
                        '    entries = json.loads(Path(project).read_text())\n'
                        '    assert all(e["file"].startswith("src/") for e in entries)\n'
                        '    assert "-j2" in sys.argv and "--std=c++20" in sys.argv\n'
                        '    if os.environ.get("TOOL_TIMEOUT") == "yes": time.sleep(20)\n'
                        '    print("public analysis complete")\n'
                        '    print(Path(os.environ["TOOL_REPORT"]).read_text(), file=sys.stderr)\n'
                        '    sys.exit(int(os.environ.get("TOOL_EXIT", "0")))\n')
        tool.chmod(0o755)

    def entry(self, name):
        return {'directory': str(self.root), 'file': name, 'command': 'c++ -c ' + name}

    def diagnostic(self, severity='error', path='src/public.cpp', identifier='outOfBounds', line='2'):
        return (f'<error id="{identifier}" severity="{severity}" msg="Public diagnostic">'
                f'<location file="{self.root / path}" line="{line}"/></error>')

    def analyze(self, errors='', raw=None, **environment):
        (self.build / 'compile_commands.json').write_text(json.dumps(self.database))
        self.report.write_text(raw if raw is not None else f'<results><errors>{errors}</errors></results>')
        real_run = subprocess.run

        def run(command, **kwargs):
            if command[0] == 'cmake':
                return subprocess.CompletedProcess(command, 0)
            return real_run(command, **kwargs)

        with mock.patch.object(cppcheck, 'ROOT', self.root), \
                mock.patch.dict(os.environ, TOOL_REPORT=str(self.report), **environment), \
                mock.patch.object(sys, 'argv', ['cppcheck.py', '--timeout', '1']), \
                mock.patch.object(cppcheck.subprocess, 'run', side_effect=run) as generator, \
                contextlib.redirect_stdout(io.StringIO()):
            result = cppcheck.main()
        generated = [call for call in generator.call_args_list if call.args[0][0] == 'cmake']
        self.assertEqual(len(generated), 2)
        for call in generated:
            self.assertEqual(call.kwargs, {'check': True, 'timeout': 30})
        return result

    def test_only_owned_reports_block_and_warnings_remain_visible(self):
        errors = self.diagnostic('warning') + self.diagnostic(path='vendor/private.cpp')
        self.assertFalse(self.analyze(errors))
        summary = json.loads((self.build / 'cppcheck-results/summary.json').read_text())
        self.assertEqual(summary['compile_commands'], 1)
        self.assertEqual(len(summary['diagnostics']), 1)
        self.assertEqual(summary['blocking_errors'], [])
        self.assertTrue(self.analyze(self.diagnostic()))
        summary = json.loads((self.build / 'cppcheck-results/summary.json').read_text())
        self.assertEqual(summary['blocking_errors'][0]['file'], 'src/public.cpp')
        self.assertTrue(self.analyze('<error id="internalError" severity="error" msg="No source location"/>'))

    def test_reviewed_exception_is_bound_to_exact_source_context(self):
        production = Path(cppcheck.ROOT) / 'profiles/mhp3rd/host/kernel/kernel.cpp'
        reviewed = production.read_text().splitlines()[818:837]
        local = self.root / 'profiles/mhp3rd/host/kernel/kernel.cpp'
        local.parent.mkdir(parents=True)
        local.write_text('\n'.join([''] * 818 + reviewed) + '\n')
        diagnostic = self.diagnostic(path='profiles/mhp3rd/host/kernel/kernel.cpp',
                                     identifier='invalidContainer', line='832')
        self.assertFalse(self.analyze(diagnostic))
        summary = json.loads((self.build / 'cppcheck-results/summary.json').read_text())
        self.assertEqual(len(summary['reviewed_exceptions']), 1)
        local.write_text('\n'.join([''] * 818 + ['Changed context'] + reviewed[1:]) + '\n')
        self.assertTrue(self.analyze(diagnostic))

    def test_failed_invocation_or_malformed_report_cannot_pass(self):
        self.assertTrue(self.analyze(TOOL_EXIT='3'))
        with self.assertRaises(cppcheck.ET.ParseError):
            self.analyze(raw='not XML')

    def test_timeout_kills_owned_process_and_keeps_diagnostics(self):
        self.assertEqual(self.analyze(TOOL_TIMEOUT='yes'), 1)
        result = self.build / 'cppcheck-results'
        self.assertTrue((result / 'progress.log').exists())
        self.assertTrue((result / 'diagnostics.xml').exists())
        self.assertIn('1s', (result / 'timeout.txt').read_text())

    def test_version_empty_scope_and_external_paths_are_rejected(self):
        with self.assertRaises(SystemExit):
            self.analyze(TOOL_VERSION='Cppcheck 2.17.1')
        self.database = [self.entry('vendor/private.cpp')]
        with self.assertRaises(SystemExit):
            self.analyze()
        with mock.patch.object(cppcheck, 'ROOT', self.root):
            self.assertIsNone(cppcheck.relative_path(self.root.parent / 'outside.cpp'))


if __name__ == '__main__':
    unittest.main()
