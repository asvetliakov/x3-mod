"""Focused production counter core and Windows translation-unit checks."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]
class BufferLockObservation(unittest.TestCase):
    def test_core_barriers_and_saturation(self):
        with tempfile.TemporaryDirectory(prefix='x3-buffer-lock-') as tmp:
            exe = pathlib.Path(tmp) / 'core'
            subprocess.run([shutil.which('clang++') or 'c++', '-std=c++17', '-O2',
                '-Wall', '-Wextra', '-Werror', '-pthread',
                str(ROOT/'verification/probe/buffer_lock_observation_core.cpp'),
                '-o', str(exe)], check=True, capture_output=True, text=True)
            run = subprocess.run([str(exe)], check=True, capture_output=True, text=True, timeout=15)
            self.assertEqual(run.stdout, 'buffer_lock_observation checks=28 failures=0\n')
            self.assertEqual(run.stderr, '')
if __name__ == '__main__':
    unittest.main()
