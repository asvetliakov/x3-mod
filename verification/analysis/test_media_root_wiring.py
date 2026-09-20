"""Actual root/group/common ingress and canonical services; no Wine/game."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
SOURCES = ('src/media/playback_runtime.cpp', 'src/proxy/media_playback.cpp',
           'src/proxy/media_startup.cpp', 'src/proxy/media_presentation_gate.cpp',
           'src/proxy/media_destination.cpp', 'src/proxy/media_engine_adapter.cpp',
           'src/proxy/media_services.cpp', 'src/proxy/media_root.cpp')
class RootWiring(unittest.TestCase):
    def test_group_ingress_readiness(self):
        with tempfile.TemporaryDirectory(prefix='x3-root-wiring-') as work:
            binary = Path(work) / 'fixture'
            command = [shutil.which('c++'), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread',
                       '-I'+str(ROOT/'src/media'), *(str(ROOT/s) for s in SOURCES),
                       str(ROOT/'verification/probe/media_root_wiring_fixture.cpp'), '-o', str(binary)]
            build = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout+build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            print(run.stdout.strip())
            self.assertEqual(run.returncode, 0, run.stdout+run.stderr)
            self.assertIn('failures=0', run.stdout)
if __name__ == '__main__':
    unittest.main()
