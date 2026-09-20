"""Actual destination core, canonical FrameLease and emitted-group transactions."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[2]

class MediaDestinationTests(unittest.TestCase):
    def test_actual_copy_provenance_and_reentry(self):
        worker = Path(os.environ.get('X3M_MEDIA_WORKER_HEADER', ROOT / 'src/media/lav_worker.h'))
        self.assertTrue(worker.is_file(), f'Canonical FrameLease header required: {worker}')
        compiler = shutil.which('clang++') or shutil.which('c++')
        with tempfile.TemporaryDirectory(prefix='x3-destination-') as directory:
            directory = Path(directory)
            # One canonical header snapshot; runtime resolves from this checkout.
            # Avoid including a second checkout's pragma-once runtime definition.
            shutil.copyfile(worker, directory / 'lav_worker.h')
            executable = directory / 'host'
            command = [compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread',
                       f'-I{directory}', f'-I{ROOT / "src/media"}',
                       str(ROOT / 'verification/probe/media_destination_host.cpp'),
                       str(ROOT / 'src/proxy/media_destination.cpp'),
                       str(ROOT / 'src/proxy/media_presentation_gate.cpp'), '-o', str(executable)]
            build = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stderr + run.stdout)
            self.assertRegex(run.stdout, r'checks=\d+ failures=0 churn=100000 reentry=25 throw=4 sites=19')
            self.assertEqual(run.stderr, '')
            print(run.stdout.strip())

if __name__ == '__main__':
    unittest.main()
