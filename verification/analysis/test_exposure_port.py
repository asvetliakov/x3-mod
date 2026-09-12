"""src/renderer/exposure.{h,cpp} against tools/analysis/exposure_reference.py.

The port is compiled natively (a host C++ compiler; no D3D, no Wine) with the
driver verification/probe/exposure_port_check.cpp and its output is replayed
through the reference: metering (decode modes, floor and clip), the reduction
chain on odd and power-of-four sizes (both channels) and to a tile image, the
space-aware statistic and target on the synthetic scenes (uniform grey, black
sky with a lit patch, a bright full frame, super-bright sparks, a lone speck
below the minimum lit fraction, a NaN tile, an edge emitter against a centre
object) unweighted and centre-weighted, the centre weights themselves, the
key rule and the limit, the dead band, dt clamps and rates, the exposure
multiplier and k, the manual override, four frame loops (the fixture's
uniform script, an offset/time-constant variant, the dead-band sequence and
the scene sequence) and the TAA weighting round trip. The stage-2
fixture then checks the GPU chain and the DLL's statistic against the same
reference (verification/probe/run_motion_output.py, hdrexposure).
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
FLOOR_LOG = float('%.9g' % math.log2(1e-4))   # the driver's float32 floor
GREY = float('%.9g' % -5.443855)
PATCH = float('%.9g' % -3.820943)


def f32(x):
    """The driver's float32 inputs, as the reference must see them."""
    return float('%.9g' % x)


def scenes():
    """The driver's synthetic tile images (16x16 tiles), in the same order."""
    out = []
    out.append(('grey', [GREY] * 256, [GREY] * 256))
    mean, peak = [FLOOR_LOG] * 256, [FLOOR_LOG] * 256
    for y in range(2, 6):
        for x in range(2, 6):
            mean[y * 16 + x] = peak[y * 16 + x] = PATCH
    out.append(('sky', mean, peak))
    out.append(('menu', [0.0] * 256, [0.0] * 256))
    mean, peak = [GREY] * 256, [GREY] * 256
    for i in (17, 70, 133, 196, 238):
        mean[i] = f32((4.0 * 6.0 + 12.0 * GREY) / 16.0); peak[i] = 6.0
    out.append(('sparks', mean, peak))
    mean, peak = [FLOOR_LOG] * 256, [FLOOR_LOG] * 256
    mean[100] = peak[100] = -2.0
    out.append(('speck', mean, peak))
    mean, peak = [GREY] * 256, [GREY] * 256
    mean[5] = peak[5] = float('nan')
    out.append(('nan', mean, peak))
    mean, peak = [FLOOR_LOG] * 256, [FLOOR_LOG] * 256
    for y in range(16):
        for x in range(8):
            mean[y * 16 + x] = peak[y * 16 + x] = 0.0
    for y in range(4, 12):
        for x in range(4, 12):
            mean[y * 16 + x] = peak[y * 16 + x] = f32(-2.473931)
    out.append(('emitter', mean, peak))
    return out


WEIGHTS = None


def weights16():
    global WEIGHTS
    if WEIGHTS is None:
        WEIGHTS = [f32(w) for w in ex.tile_weights(16, 16)]
    return WEIGHTS


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
        reference = [ex.KEY, ex.EV_OFFSET, ex.EV_MIN, ex.EV_MAX, ex.TAU_UP, ex.TAU_DOWN, ex.DT_MIN, ex.DT_MAX, ex.METER_FLOOR, ex.METER_CLIP,
                     ex.METER_BG, ex.METER_MIN_LIT, ex.WHITE_TARGET, ex.KEY_PULL, ex.EV_DEADBAND, ex.EDGE_WEIGHT, ex.TONEMAP_WHITE, ex.TILE_MAX]
        self.assertEqual(len(p), len(reference))
        for a, b in zip(p, reference):
            self.assertAlmostEqual(a, b, delta=1e-6 * max(1.0, abs(b)))  # the port stores float32
        self.assertEqual((ex.EV_MIN, ex.EV_MAX), (-3.0, 2.0))

    def test_meter_level0_and_clip(self):
        modes = ['gamma2.2', 'srgb', 'none']
        rows = self.lines['meter']
        self.assertEqual(len(rows), 18)
        for m, r, g, b, value, clipped in rows:
            rgb = (float(r), float(g), float(b))
            self.assertAlmostEqual(float(value), ex.meter_level0(rgb, modes[int(m)]), delta=2e-5, msg=(m, rgb))
            self.assertEqual(int(clipped), int(ex.meter_clipped(rgb, modes[int(m)])), (m, rgb))

    def test_reduce_chain_matches_reference(self):
        for w, h, mean, chain, peak, rw, rh in self.lines['chain']:
            w, h = int(w), int(h)
            level = [f32(-6.0 + 0.1 * (i % 17) + 0.01 * i) for i in range(w * h)]
            self.assertEqual((int(rw), int(rh)), (1, 1))
            self.assertAlmostEqual(float(mean), ex.reduce_mean(level), delta=1e-5)
            self.assertAlmostEqual(float(chain), ex.reduce_chain(level, w, h), delta=1e-4)
            self.assertAlmostEqual(float(peak), max(level), delta=1e-6)   # the max channel reduces to the maximum
            if w == 16:
                self.assertAlmostEqual(float(chain), float(mean), delta=1e-4)  # a power of four reduces to the mean

    def test_reduce_to_tile_image(self):
        row = self.lines['tiles'][0]
        tw, th = int(row[0]), int(row[1])
        w, h = 40, 24
        level = [f32(-8.0 + 0.05 * (i % 23)) for i in range(w * h)]
        peak = [f32(level[i] + 0.5 * (i % 3)) for i in range(w * h)]
        means, _, rw, rh = ex.reduce_tiles(level, w, h, 4, 16)
        _, maxes, _, _ = ex.reduce_tiles(peak, w, h, 4, 16)
        self.assertEqual((tw, th), (rw, rh), (tw, th))
        self.assertEqual((tw, th), (10, 6))
        values = [float(v) for v in row[2:]]
        self.assertEqual(len(values), 2 * tw * th)
        for i in range(tw * th):
            self.assertAlmostEqual(values[2 * i], means[i], delta=1e-5, msg=i)
            self.assertAlmostEqual(values[2 * i + 1], maxes[i], delta=1e-6, msg=i)

    def test_centre_weights(self):
        values = [float(v) for v in self.lines['weights'][0]]
        reference = ex.tile_weights(16, 16)
        self.assertEqual(len(values), 256)
        for a, b in zip(values, reference):
            self.assertAlmostEqual(a, b, delta=2e-6)
        self.assertGreater(min(values), ex.EDGE_WEIGHT - 1e-6)     # the corners approach the edge weight
        self.assertLess(values[0], 0.36)
        self.assertGreater(values[8 * 16 + 8], 0.98)               # the centre is (nearly) 1
        self.assertEqual(values[0], values[15]); self.assertEqual(values[0], values[255])   # symmetric

    def test_statistics_and_targets_on_scenes(self):
        expected_target = {'grey': ex.EV_MAX, 'sky': None, 'menu': None, 'sparks': None, 'speck': 0.0, 'nan': ex.EV_MAX, 'emitter': None}
        for tag, weights in (('stats', None), ('wstats', weights16())):
            rows = {r[0]: r[1:] for r in self.lines[tag]}
            for name, mean, peak in scenes():
                tiles, lit, avg, fraction, median, lit_mean, p99, neutral, ev_key, ev_limit, ev_target, lit_weight = rows[name]
                ref = ex.meter_statistics(mean, peak, weights=weights)
                target = ex.exposure_target(ref)
                self.assertEqual((int(tiles), int(lit), int(neutral)), (ref['tiles'], ref['lit'], int(ref['neutral'])), (tag, name))
                self.assertAlmostEqual(float(avg), ref['avg_log_l'], delta=1e-5, msg=(tag, name))
                self.assertAlmostEqual(float(fraction), ref['lit_fraction'], delta=1e-7, msg=(tag, name))
                self.assertAlmostEqual(float(lit_weight), ref['lit_weight'], delta=1e-4, msg=(tag, name))
                self.assertAlmostEqual(float(median), ref['lit_median_log'], delta=1e-6, msg=(tag, name))
                self.assertAlmostEqual(float(lit_mean), ref['lit_mean_log'], delta=1e-5, msg=(tag, name))
                self.assertAlmostEqual(float(p99), ref['p99_max_log'], delta=1e-6, msg=(tag, name))
                self.assertAlmostEqual(float(ev_key), target['ev_key'], delta=1e-5, msg=(tag, name))
                self.assertAlmostEqual(float(ev_limit), target['ev_limit'], delta=1e-5, msg=(tag, name))
                self.assertAlmostEqual(float(ev_target), target['ev_target'], delta=1e-5, msg=(tag, name))
                if expected_target[name] is not None:
                    self.assertAlmostEqual(float(ev_target), expected_target[name], delta=1e-5, msg=(tag, name))
        # The emitter: centre-weighted the object (at the key) sets the target, EV 0; unweighted the emitter does, -0.62.
        _, mean, peak = scenes()[6]
        weighted = ex.exposure_target(ex.meter_statistics(mean, peak, weights=weights16()))
        unweighted = ex.exposure_target(ex.meter_statistics(mean, peak))
        self.assertAlmostEqual(weighted['ev_target'], 0.0, delta=1e-5)
        self.assertAlmostEqual(unweighted['ev_target'], ex.KEY_PULL * math.log2(ex.KEY), delta=1e-5)
        self.assertAlmostEqual(weighted['ev_limit'], unweighted['ev_limit'])   # the highlight limit stays unweighted
        # The design's acceptance on the scenes: the patch is lifted by the key
        # rule only (to the key), the menu is pulled down gently, the sparks
        # engage the highlight limit, the speck is neutral, the NaN tile is a floor tile.
        sky = ex.exposure_target(ex.meter_statistics(scenes()[1][1], scenes()[1][2]))
        self.assertAlmostEqual(sky['ev_key'], math.log2(ex.KEY) - PATCH, delta=1e-6)
        self.assertLess(sky['ev_key'], sky['ev_limit'])
        menu = ex.exposure_target(ex.meter_statistics(scenes()[2][1], scenes()[2][2]))
        self.assertAlmostEqual(menu['ev_target'], ex.KEY_PULL * math.log2(ex.KEY), delta=1e-6)
        self.assertGreater(menu['ev_target'], -1.0)
        sparks = ex.exposure_target(ex.meter_statistics(scenes()[3][1], scenes()[3][2]))
        self.assertAlmostEqual(sparks['ev_limit'], math.log2(ex.WHITE_TARGET * ex.TONEMAP_WHITE) - 6.0, delta=1e-6)
        self.assertEqual(sparks['ev_target'], sparks['ev_limit'])
        self.assertLess(sparks['ev_limit'], sparks['ev_key'])
        self.assertTrue(ex.meter_statistics(scenes()[4][1], scenes()[4][2])['neutral'])
        self.assertEqual(ex.meter_statistics(scenes()[5][1], scenes()[5][2])['lit'], 255)

    def test_target_dt_exposure_resolve(self):
        for v, key, limit, target in self.lines['target']:
            v = float(v)
            self.assertAlmostEqual(float(key), ex.ev_key(v), delta=1e-5)
            self.assertAlmostEqual(float(limit), ex.ev_limit(v), delta=1e-5)
            self.assertAlmostEqual(float(target), ex.ev_target(ex.uniform_statistics(v)), delta=1e-5)
        for dt, clamped, up, down in self.lines['dt']:
            self.assertAlmostEqual(float(clamped), ex.clamp_dt(float(dt)), delta=1e-7)
            self.assertAlmostEqual(float(up), ex.adapt_rate(float(dt), ex.TAU_UP), delta=1e-6)
            self.assertAlmostEqual(float(down), ex.adapt_rate(float(dt), ex.TAU_DOWN), delta=1e-6)
        for ev, mult, k in self.lines['exposure']:
            self.assertAlmostEqual(float(mult), ex.exposure_multiplier(float(ev)), delta=1e-5 * max(1.0, 2.0 ** float(ev)))
            self.assertAlmostEqual(float(k), ex.taa_k(ex.exposure_multiplier(float(ev))), delta=1e-5 * max(1.0, 2.0 ** float(ev)))
        manual, auto = (float(v) for v in self.lines['resolve'][0])
        self.assertEqual((manual, auto), (ex.resolve_ev('manual', -2.0, 1.5), ex.resolve_ev('auto', -2.0, 1.5)))
        rows = self.lines['deadband']
        self.assertEqual(len(rows), 27)
        for held, fresh, moved, first in rows:
            held, fresh = float(held), float(fresh)
            self.assertAlmostEqual(float(moved), ex.apply_deadband(held, fresh), delta=1e-6, msg=(held, fresh))
            self.assertAlmostEqual(float(first), ex.apply_deadband(None, fresh), delta=1e-6, msg=(held, fresh))
            self.assertEqual(abs(float(moved) - fresh) < 1e-6, abs(fresh - held) > ex.EV_DEADBAND or fresh == held, (held, fresh))

    def test_frame_loops_match_simulate(self):
        values = [float(v) for v in self.lines['simulate'][0]]
        meters = [GREY] * 10 + [f32(-0.246413)] * 10 + [f32(-2.582891)] * 10
        reference = ex.simulate(meters, [0.016] * 30)
        reference.append(ex.adapt(reference[-1], ex.ev_target(ex.uniform_statistics(meters[-1])), 0.016))
        self.assertEqual(len(values), 31)
        for a, b in zip(values, reference):
            self.assertAlmostEqual(a, b, delta=1e-5)
        values = [float(v) for v in self.lines['simulate_offset'][0]]
        meters = [GREY] * 5 + [f32(-0.246413)] * 5
        reference = ex.simulate(meters, [0.033] * 10, ev_offset=1.0, tau_up=0.2, tau_down=0.6)
        reference.append(ex.adapt(reference[-1], ex.ev_target(ex.uniform_statistics(meters[-1]), ev_offset=1.0), 0.033, 0.2, 0.6))
        for a, b in zip(values, reference):
            self.assertAlmostEqual(a, b, delta=1e-5)
        self.assertGreater(values[5], values[1])   # dark scene: EV rises
        self.assertLess(values[10], values[6])     # bright scene: EV falls
        # The dead band: the held target stays through the two small changes and moves on the large one.
        values = [float(v) for v in self.lines['simulate_deadband'][0]]
        levels = [f32(v) for v in (-3.32, -3.32, -3.32, -3.16, -3.16, -3.52, -3.52, -3.32, -1.62, -1.62, -1.75)]
        targets = []
        reference = ex.simulate(levels, [0.2] * len(levels), targets=targets)
        reference.append(ex.adapt(reference[-1], targets[-1], 0.2))
        self.assertEqual(len(values), 3 * len(levels))
        for i, v in enumerate(levels):
            fresh, held, ev = values[3 * i:3 * i + 3]
            self.assertAlmostEqual(fresh, ex.ev_target(ex.uniform_statistics(v)), delta=1e-5, msg=i)
            self.assertAlmostEqual(held, targets[i], delta=1e-5, msg=i)
            self.assertAlmostEqual(ev, reference[i + 1], delta=1e-5, msg=i)
        held = [values[3 * i + 1] for i in range(len(levels))]
        self.assertEqual(len(set(held[:8])), 1)                    # 0.35 / 0.37 / 0.33: within the band, one held target
        self.assertNotEqual(held[8], held[7])                       # 0.6: moved
        self.assertAlmostEqual(held[8], held[10], delta=1e-6)       # 0.55-ish (-1.75): within the band of the new target
        values = [float(v) for v in self.lines['simulate_scenes'][0]]
        stats = [ex.meter_statistics(mean, peak, weights=weights16()) for _, mean, peak in scenes() for _ in range(10)]
        targets = []
        reference = ex.simulate(stats, [0.016] * len(stats), targets=targets)
        reference.append(ex.adapt(reference[-1], targets[-1], 0.016))
        self.assertEqual(len(values), 71)
        for a, b in zip(values, reference):
            self.assertAlmostEqual(a, b, delta=1e-5)

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
