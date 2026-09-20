"""Actual consumer/encoder/rollback sources; no Wine or game execution."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
SOURCES = [ROOT / 'src/media/playback_runtime.cpp', ROOT / 'src/proxy/media_playback.cpp',
           ROOT / 'src/proxy/media_engine_adapter.cpp']
class MediaEngineAdapter(unittest.TestCase):
    def test_semantics_and_transaction_failures(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        with tempfile.TemporaryDirectory(prefix='x3-media-engine-') as work:
            out = Path(work) / 'fixture'
            command = [compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                       *map(str, SOURCES), str(ROOT / 'verification/probe/media_engine_adapter_fixture.cpp'), '-o', str(out)]
            build = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(out)], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('failures=0', run.stdout)
            print(run.stdout.strip())
    def test_native_windows_compilation(self):
        compiler = shutil.which('i686-w64-mingw32-g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix='x3-media-engine-x86-') as work:
            for source in SOURCES + [ROOT / 'src/proxy/media_cue.cpp']:
                command = [compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                           '-msse2', '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2',
                           '-fno-exceptions', '-c', str(source), '-o', str(Path(work) / (source.stem + '.o'))]
                build = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
if __name__ == '__main__':
    unittest.main()
