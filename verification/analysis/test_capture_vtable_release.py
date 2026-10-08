"""Execute capture.cpp's Hooks::release_original on the host (the DXVK teardown fault class).

The production `struct Hooks` is extracted unchanged and compiled against a C++-style COM
double whose final Release reads its deleting destructor through the object's current
vtable pointer, past the slots the proxy copies (verification/results/dxvk-teardown-crash/
witness.txt). Host control flow only, not the Windows ABI or a real backend.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from source_text import source_text
from test_capture_bloom_lifetime import extract_function

ROOT = Path(__file__).resolve().parents[2]


class CaptureVtableReleaseTests(unittest.TestCase):
    def test_final_release_runs_with_the_backend_table(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        hooks = extract_function(source_text(ROOT / 'src/proxy/capture.cpp'), 'struct Hooks') + ';'
        with tempfile.TemporaryDirectory(prefix='x3-capture-vtable-release-') as temporary:
            directory = Path(temporary)
            (directory / 'capture_vtable_release_under_test_inc.h').write_text(hooks)
            executable = directory / 'capture_vtable_release'
            build = subprocess.run([
                compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(directory),
                str(ROOT / 'verification/probe/capture_vtable_release_fixture.cpp'), '-o', str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout, 'capture_vtable_release scenarios=5 checks=14 failures=0\n')


if __name__ == '__main__':
    unittest.main()
