"""Compile public Java fixtures through the real two-stage CodeQL helper."""

import contextlib
import io
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

import android_java


@unittest.skipUnless(shutil.which('javac'), 'Requires a JDK')
class AndroidJavaCompilerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.jar = self.root / 'android fixture.jar'
        with zipfile.ZipFile(self.jar, 'w') as archive:
            archive.writestr('META-INF/MANIFEST.MF', 'Manifest-Version: 1.0\n')
        self.sdl = self.root / 'sdl source'
        self.sdl_source = self.sdl / 'android-project/app/src/main/java/org/libsdl/app/SDLActivity.java'
        self.sdl_source.parent.mkdir(parents=True)
        self.sdl_source.write_text('package org.libsdl.app; public class SDLActivity { public int value() { return 7; } }\n')
        self.app = self.root / 'profiles/mhp3rd/packaging/android/java/PublicActivity.java'
        self.app.parent.mkdir(parents=True)
        self.app.write_text('public class PublicActivity extends org.libsdl.app.SDLActivity {}\n')
        self.build = self.root / 'build with spaces'

    def compile(self, stage='all'):
        arguments = ['android_java.py', str(self.jar), str(self.sdl), str(self.build), '--stage', stage]
        with mock.patch.object(android_java, '__file__', str(self.root / 'scripts/ci/android_java.py')), \
                mock.patch.object(sys, 'argv', arguments), contextlib.redirect_stdout(io.StringIO()) as output:
            android_java.main()
        return output.getvalue()

    def test_all_stages_resolve_external_classes_and_emit_java11_bytecode(self):
        output = self.compile()
        self.assertIn('1 sdl Java sources', output)
        self.assertIn('1 app Java sources', output)
        for file in ('sdl-classes/org/libsdl/app/SDLActivity.class', 'app-classes/PublicActivity.class'):
            header = (self.build / file).read_bytes()[:8]
            self.assertEqual(header[:4], bytes.fromhex('cafebabe'))
            self.assertEqual(int.from_bytes(header[6:8], 'big'), 55)

    def test_sdl_and_app_stages_can_run_separately(self):
        self.compile('sdl')
        self.assertFalse((self.build / 'app-classes').exists())
        self.compile('app')
        self.assertTrue((self.build / 'app-classes/PublicActivity.class').is_file())

    def test_app_without_sdl_or_sources_is_rejected(self):
        with self.assertRaisesRegex(SystemExit, 'Compile the SDL stage'):
            self.compile('app')
        self.app.unlink()
        with self.assertRaisesRegex(SystemExit, 'No Java sources'):
            self.compile('app')
        self.sdl_source.unlink()
        with self.assertRaisesRegex(SystemExit, 'No Java sources'):
            self.compile('sdl')

    def test_actual_compiler_failure_propagates_without_building_app(self):
        self.sdl_source.write_text('public class InvalidDeclaration {}\n')
        with self.assertRaises(subprocess.CalledProcessError):
            self.compile()
        self.assertFalse((self.build / 'app-classes').exists())


if __name__ == '__main__':
    unittest.main()
