"""Host-test the allocation-free consolidated native timing state machine."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ChaseNativeTimingTests(unittest.TestCase):
    def test_phase_lifetime_capacity_and_window_contract(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-chase-native-timing-') as temporary:
            executable = Path(temporary) / 'chase_native_timing_host'
            build = subprocess.run([
                compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / 'src/proxy'),
                str(ROOT / 'verification/probe/chase_native_timing_host.cpp'), '-o', str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout,
                             'chase_native_timing_host scenarios=12 checks=34 failures=0\n')
            self.assertEqual(run.stderr, '')


if __name__ == '__main__':
    unittest.main()
