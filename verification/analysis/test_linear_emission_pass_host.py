"""Compile the whole emission pass against scripted public host D3D interfaces.

Exercises control flow and COM reference accounting only, not GPU pixels or
native Windows ABI. No Wine runner, production DLL build or evidence manifest.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class LinearEmissionPassHostTests(unittest.TestCase):
    def test_control_flow_and_com_outputs(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-emission-host-') as temporary:
            executable = Path(temporary) / 'pass-host'
            command = [compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                       '-DX3M_LINEAR_EMISSION_PASS_FIXTURE',
                       '-I', str(ROOT / 'verification/probe/linear_emission_pass_stubs'),
                       str(ROOT / 'verification/probe/linear_emission_pass_host.cpp'),
                       str(ROOT / 'src/renderer/linear_emission_pass.cpp'),
                       '-o', str(executable)]
            build = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual(run.stdout, 'linear_emission_pass_host scenarios=67 checks=1573 failures=0\n')
            self.assertEqual(run.stderr, '')


if __name__ == '__main__':
    unittest.main()
