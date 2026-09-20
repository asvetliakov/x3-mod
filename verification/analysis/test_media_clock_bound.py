"""Execute bound-only transactions against the actual canonical Clock header."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'verification/probe/media_clock_bound_fixture.cpp'


class ClockBound(unittest.TestCase):
    def test_canonical_clock_bound_transactions(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix='x3-clock-bound-') as temporary:
            exe = Path(temporary) / 'fixture'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                    '-pedantic', str(FIXTURE), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertRegex(run.stdout, r'^media_clock_bound checks=\d+ failures=0 clock_bytes=\d+\n$')
            print(run.stdout.strip())

    def test_i686_windows_compile(self):
        compiler = shutil.which('i686-w64-mingw32-g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix='x3-clock-bound-i686-') as temporary:
            obj = Path(temporary) / 'fixture.o'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                    '-pedantic', '-msse2', '-mfpmath=sse', '-mstackrealign',
                                    '-mincoming-stack-boundary=2', '-c', str(FIXTURE), '-o', str(obj)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)


if __name__ == '__main__':
    unittest.main()
