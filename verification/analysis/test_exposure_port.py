"""src/renderer/exposure.{h,cpp} against tools/analysis/exposure_reference.py.

The port is compiled natively (a host C++ compiler; no D3D, no Wine) with the
driver verification/probe/exposure_port_check.cpp and its output is replayed
through the reference: metering (decode modes, floor and clip), the reduction
chain on odd and power-of-four sizes, targets and clamps, dt clamps and rates,
the exposure multiplier and k, the manual override, two frame loops (the
fixture's 30-frame script and an offset/time-constant variant) and the TAA
weighting round trip. The stage-2 fixture then checks the GPU chain against the
same reference (verification/probe/run_motion_output.py, hdrexposure).
"""
import math
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import exposure_reference as ex  # noqa: E402

DRIVER = ROOT / 'verification/probe/exposure_port_check.cpp'
SOURCE = ROOT / 'src/renderer/exposure.cpp'
COMPILERS = ('c++', 'clang++', 'g++')


def compile_and_run():
    compiler = next((c for c in COMPILERS if shutil.which(c)), None)
    if compiler is None:
        raise unittest.SkipTest('no host C++ compiler')
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / 'exposure_port_check'
        subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', str(DRIVER), str(SOURCE), '-o', str(exe)], check=True)
        out = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout
    lines = {}
    for line in out.splitlines():
        key, *values = line.split()
        lines.setdefault(key, []).append(values)
    return lines


class ExposurePortTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lines = compile_and_run()

    def test_defaults_match(self):
        p = [float(v) for v in self.lines['params'][0]]
        reference = [ex.KEY, ex.EV_OFFSET, ex.EV_MIN, ex.EV_MAX, ex.TAU_UP, ex.TAU_DOWN, ex.DT_MIN, ex.DT_MAX, ex.METER_FLOOR, ex.METER_CLIP]
        for a, b in zip(p, reference):
            self.assertAlmostEqual(a, b, delta=1e-6 * max(1.0, abs(b)))  # the port stores float32

    def test_meter_level0_and_clip(self):
        modes = ['gamma2.2', 'srgb', 'none']
        rows = self.lines['meter']
        self.assertEqual(len(rows), 18)
        for m, r, g, b, value, clipped in rows:
            rgb = (float(r), float(g), float(b))
            self.assertAlmostEqual(float(value), ex.meter_level0(rgb, modes[int(m)]), delta=2e-5, msg=(m, rgb))
            self.assertEqual(int(clipped), int(ex.meter_clipped(rgb, modes[int(m)])), (m, rgb))

    def test_reduce_chain_matches_reference(self):
        for w, h, mean, chain in self.lines['chain']:
            w, h = int(w), int(h)
            level = [-6.0 + 0.1 * (i % 17) + 0.01 * i for i in range(w * h)]
            level = [float('%.9g' % v) for v in level]  # the driver's float32 inputs
            self.assertAlmostEqual(float(mean), ex.reduce_mean(level), delta=1e-5)
            self.assertAlmostEqual(float(chain), ex.reduce_chain(level, w, h), delta=1e-4)
            if w == 16:
                self.assertAlmostEqual(float(chain), float(mean), delta=1e-4)  # a power of four reduces to the mean

    def test_target_dt_exposure_resolve(self):
        for avg, target in self.lines['target']:
            self.assertAlmostEqual(float(target), ex.ev_target(float(avg)), delta=1e-5)
        for dt, clamped, up, down in self.lines['dt']:
            self.assertAlmostEqual(float(clamped), ex.clamp_dt(float(dt)), delta=1e-7)
            self.assertAlmostEqual(float(up), ex.adapt_rate(float(dt), ex.TAU_UP), delta=1e-6)
            self.assertAlmostEqual(float(down), ex.adapt_rate(float(dt), ex.TAU_DOWN), delta=1e-6)
        for ev, mult, k in self.lines['exposure']:
            self.assertAlmostEqual(float(mult), ex.exposure_multiplier(float(ev)), delta=1e-5 * max(1.0, 2.0 ** float(ev)))
            self.assertAlmostEqual(float(k), ex.taa_k(ex.exposure_multiplier(float(ev))), delta=1e-5 * max(1.0, 2.0 ** float(ev)))
        manual, auto = (float(v) for v in self.lines['resolve'][0])
        self.assertEqual((manual, auto), (ex.resolve_ev('manual', -2.0, 1.5), ex.resolve_ev('auto', -2.0, 1.5)))

    def test_frame_loops_match_simulate(self):
        values = [float(v) for v in self.lines['simulate'][0]]
        meters = [-5.443855] * 10 + [-0.246413] * 10 + [-2.582891] * 10
        reference = ex.simulate(meters, [0.016] * 30)
        reference.append(ex.adapt(reference[-1], ex.ev_target(meters[-1]), 0.016))
        self.assertEqual(len(values), 31)
        for a, b in zip(values, reference):
            self.assertAlmostEqual(a, b, delta=1e-5)
        values = [float(v) for v in self.lines['simulate_offset'][0]]
        meters = [-5.443855] * 5 + [-0.246413] * 5
        reference = ex.simulate(meters, [0.033] * 10, ev_offset=1.0, tau_up=0.2, tau_down=0.6)
        reference.append(ex.adapt(reference[-1], ex.ev_target(meters[-1], ev_offset=1.0), 0.033, 0.2, 0.6))
        for a, b in zip(values, reference):
            self.assertAlmostEqual(a, b, delta=1e-5)
        self.assertGreater(values[5], values[1])   # dark scene: EV rises
        self.assertLess(values[10], values[6])     # bright scene: EV falls

    def test_weighting_round_trip(self):
        weighted = tuple(float(v) for v in self.lines['weighted'][0])
        reference = ex.weight_color((2.0, 40.0, 0.5), 1.5)
        for a, b in zip(weighted, reference):
            self.assertAlmostEqual(a, b, delta=1e-5)
        r, g, b, w = (float(v) for v in self.lines['unweighted'][0])
        self.assertAlmostEqual(r, 2.0, delta=1e-4); self.assertAlmostEqual(g, 40.0, delta=1e-3); self.assertAlmostEqual(b, 0.5, delta=1e-4)
        self.assertAlmostEqual(w, ex.luma_weight(0.5, 2.0), delta=1e-7)
        self.assertTrue(math.isfinite(w))


if __name__ == '__main__':
    unittest.main()
