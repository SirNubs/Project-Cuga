"""Verify profile accumulation through the actual Make recipes."""

import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(importlib.util.find_spec('coverage') and shutil.which('make'),
                     'Run this collector regression in the coverage environment')
class CoveragePipelineTests(unittest.TestCase):
    def test_final_export_retains_initial_suites_and_later_collector_profiles(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)

            def write(name, text):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)

            write('.coveragerc', '[run]\nbranch=True\nrelative_files=True\nsource=owned\n'
                  'patch=subprocess\ndata_file=out/python-coverage/.coverage\n'
                  '[json]\noutput=out/python-coverage/coverage.json\n'
                  '[html]\ndirectory=out/python-coverage/html\n')
            write('owned/__init__.py', '')
            for name in ('archive', 'unit', 'collector'):
                write(f'owned/{name}.py', 'VALUE = 1\n')
            write('profiles/mhp3rd/tests/tool_security_tests.py',
                  'import sys\nfrom pathlib import Path\n'
                  'sys.path.insert(0, str(Path(__file__).resolve().parents[3]))\nimport owned.archive\n')
            write('scripts/ci/test_probe.py',
                  'import sys\nimport unittest\nfrom pathlib import Path\n'
                  'sys.path.insert(0, str(Path(__file__).resolve().parents[2]))\nimport owned.unit\n'
                  'class Probe(unittest.TestCase):\n'
                  '    def test_public_value(self):\n'
                  '        self.assertEqual(owned.unit.VALUE, 1)\n')
            # This fixture isolates collection from native compilation. Real
            # report parsing/gating has separate real-Git integration tests.
            write('scripts/ci/project_coverage.py',
                  'import sys\nfrom pathlib import Path\n'
                  'sys.path.insert(0, str(Path(__file__).resolve().parents[2]))\nimport owned.collector\n')
            environment = dict(os.environ, COVERAGE_RCFILE=str(root / '.coveragerc'))
            for target in ('python-coverage', 'project-coverage'):
                result = subprocess.run(['make', '-f', str(ROOT / 'Makefile'), target,
                                         f'COVERAGE_PYTHON={sys.executable}', 'MAKE=true'],
                                        cwd=root, env=environment, capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            data = json.loads((root / 'out/python-coverage/coverage.json').read_text())
            for name in ('archive', 'unit', 'collector'):
                self.assertEqual(data['files'][f'owned/{name}.py']['executed_lines'], [1], name)


if __name__ == '__main__':
    unittest.main()
