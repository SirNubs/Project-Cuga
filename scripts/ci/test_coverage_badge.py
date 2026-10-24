"""Exercise the actual badge publication shell against synthetic API responses."""

import json
import os
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(os.name == 'posix' and shutil.which('jq') and shutil.which('bash'),
                     'Exercises the Linux publisher with POSIX executable fixtures')
class CoverageBadgeTests(unittest.TestCase):
    def publish(self, parent):
        workflow = (ROOT / '.github/workflows/coverage.yml').read_text()
        script = textwrap.dedent(workflow.split('      - name: Publish only the badge JSON')[1]
                                 .split('        run: |\n', 1)[1])
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            client = directory / 'gh'
            client.write_text('#!/usr/bin/env python3\n' + textwrap.dedent('''\
                import json, os, sys
                from pathlib import Path
                args = sys.argv[1:]
                endpoint = next(a for a in args if a.startswith('repos/'))
                if endpoint.endswith('/ref/heads/coverage-badges'):
                    if os.environ['PARENT'] == 'missing':
                        print('{"message":"Not Found"}')
                        sys.exit(1)
                    print(os.environ['PARENT'])
                elif endpoint.endswith('/trees'):
                    tree = json.load(sys.stdin)
                    assert [entry['path'] for entry in tree['tree']] == ['coverage.json']
                    assert json.loads(tree['tree'][0]['content'])['message'] == '28.98%'
                    print('a' * 40)
                elif endpoint.endswith('/commits'):
                    Path(os.environ['RECORD']).write_text(sys.stdin.read())
                    print('b' * 40)
                elif '/refs' in endpoint:
                    assert 'sha=' + 'b' * 40 in args
                else:
                    raise AssertionError(endpoint)
                '''))
            client.chmod(0o755)
            record = directory / 'commit.json'
            environment = dict(os.environ, PATH=str(directory) + os.pathsep + os.environ['PATH'],
                               PARENT=parent, RECORD=str(record), GH_REPO='example/public',
                               BADGE_JSON=json.dumps({'message': '28.98%'}))
            result = subprocess.run(['bash', '-c', script], env=environment,
                                    capture_output=True, text=True, timeout=10)
            return result, json.loads(record.read_text()) if record.exists() else None

    def test_missing_branch_error_body_is_not_a_parent_commit(self):
        result, commit = self.publish('missing')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(commit['parents'], [])

    def test_existing_branch_retains_commit_history(self):
        result, commit = self.publish('c' * 40)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(commit['parents'], ['c' * 40])

    def test_invalid_success_response_aborts_before_writing(self):
        result, commit = self.publish('not-a-commit')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Invalid badge parent commit', result.stderr)
        self.assertIsNone(commit)


if __name__ == '__main__':
    unittest.main()
