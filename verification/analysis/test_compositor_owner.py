"""Exercise the production owner lookup over a hostile synthetic x86 map."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class OwnerLookup(unittest.TestCase):
    def test_production_lookup(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-compositor-owner-') as temporary:
            executable = pathlib.Path(temporary) / 'owner'
            subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'verification/probe/compositor_owner_fixture.cpp'),
                            '-o', str(executable)], check=True, capture_output=True, text=True)
            run = subprocess.run([str(executable)], check=True, capture_output=True, text=True)
            self.assertEqual(run.stdout, 'compositor_owner checks=24 failures=0\n')
            self.assertEqual(run.stderr, '')


if __name__ == '__main__':
    unittest.main()
