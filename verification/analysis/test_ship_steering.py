"""Compile and run the host double of the ship steering core (verification/probe/ship_steering_host.cpp): the engine
model's controller against the twelve settle times of axis_controller_sim.txt, the unit checks and the turn / arrival /
formation / degraded scenarios on the five representative ships at 30/60/144 fps and three seeds. No Wine, no game."""
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]


class ShipSteeringCore(unittest.TestCase):
    def test_host_fixture(self):
        with tempfile.TemporaryDirectory(prefix='x3-ship-steering-') as d:
            exe = Path(d) / 'fixture'
            build = subprocess.run(['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                    str(ROOT / 'verification/probe/ship_steering_host.cpp'), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('checks PASS', run.stdout)
            self.assertIn('failures=0', run.stdout)


if __name__ == '__main__':
    unittest.main()
