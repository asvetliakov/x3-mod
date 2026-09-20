"""Portable production lease core with fake COM cleanup; no GPU/native ABI claim."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SurfaceLeaseHostTests(unittest.TestCase):
    def test_registry_identity_and_lifetime(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix='x3-surface-lease-') as temporary:
            executable = Path(temporary) / 'surface-lease-host'
            build = subprocess.run([
                compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread',
                str(ROOT / 'verification/probe/surface_lease_host.cpp'), '-o', str(executable)
            ], capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stderr)
            match = re.fullmatch(r'surface_lease_host races=512 acquired=(\d+) refused=(\d+) checks=(\d+) failures=0\n', run.stdout)
            self.assertIsNotNone(match, run.stdout)
            self.assertEqual(int(match[1]) + int(match[2]), 512)
            self.assertGreater(int(match[3]), 2000)
            self.assertEqual(run.stderr, '')


if __name__ == '__main__':
    unittest.main()
