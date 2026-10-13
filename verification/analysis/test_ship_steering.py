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

    def test_summary_gates_bound(self):
        # B.4: the summary's gates text at its widest fits its buffer, and the source's declared figure (static_assert'ed) is current
        run = subprocess.run(['python3', '-I', str(ROOT / 'verification/results/ship-steering/gate-traffic-controller/summary_bound.py'),
                              str(ROOT / 'src/proxy/ship_steering.cpp')], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn('PASS', run.stdout)

    def test_close_probe_never_suppresses_inside_the_stopping_distance(self):
        # the step-2 review's probe: a head-on ship 45..200 m ahead of an M5 at 100 m/s (D_stop 250 m) is never a resolved pair
        with tempfile.TemporaryDirectory(prefix='x3-ship-close-') as d:
            exe = Path(d) / 'probe'
            build = subprocess.run(['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-I', str(ROOT / 'src/proxy'),
                                    str(ROOT / 'verification/results/ship-steering/close_probe.cpp'), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            lines = [l for l in run.stdout.splitlines() if l.startswith('d=')]
            self.assertEqual(len(lines), 5, run.stdout)
            self.assertTrue(all('suppressed=0' in l and 'n_resolved=0' in l for l in lines), run.stdout)


if __name__ == '__main__':
    unittest.main()
