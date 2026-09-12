"""Exposure model host reference (design §3): metering, adaptation, clamps, override, TAA weighting."""
import math
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import exposure_reference as ex  # noqa: E402


class TargetTests(unittest.TestCase):
    def test_key_maps_to_zero_ev(self):
        self.assertAlmostEqual(ex.ev_target(math.log2(0.18)), 0.0)
        self.assertAlmostEqual(ex.ev_target(math.log2(0.045)), 2.0)
        self.assertAlmostEqual(ex.ev_target(math.log2(0.18), ev_offset=-1.0), -1.0)
        self.assertAlmostEqual(ex.ev_target(math.log2(0.36), key=0.36), 0.0)

    def test_target_clamps(self):
        self.assertEqual(ex.ev_target(-40.0), ex.EV_MAX)
        self.assertEqual(ex.ev_target(40.0), ex.EV_MIN)
        self.assertEqual(ex.ev_target(0.0, ev_min=-1.0, ev_max=1.0), -1.0)
        with self.assertRaises(ValueError):
            ex.ev_target(0.0, key=0.0)
        with self.assertRaises(ValueError):
            ex.ev_target(float('nan'))


class AdaptationTests(unittest.TestCase):
    def test_time_constants(self):
        # After tau seconds a first-order filter has covered 1 - 1/e of the distance.
        target = 3.0
        for tau, tau_up, tau_down in ((0.4, 0.4, 1.2), (1.2, 0.4, 1.2)):
            ev = 0.0 if tau == tau_up else 6.0
            t = 0.0
            while t < tau - 1e-9:
                ev = ex.adapt(ev, target, 0.01, tau_up, tau_down)
                t += 0.01
            covered = abs(ev - (0.0 if tau == tau_up else 6.0)) / 3.0
            self.assertAlmostEqual(covered, 1 - math.exp(-1), delta=0.002, msg=tau)

    def test_asymmetry_and_direction(self):
        up = ex.adapt(0.0, 1.0, 0.1)
        down = ex.adapt(1.0, 0.0, 0.1)
        self.assertGreater(up, 1.0 - down)  # brightening (tau_up 0.4) is faster than darkening (tau_down 1.2)
        self.assertAlmostEqual(up, ex.adapt_rate(0.1, ex.TAU_UP))
        self.assertAlmostEqual(1.0 - down, ex.adapt_rate(0.1, ex.TAU_DOWN))
        self.assertEqual(ex.adapt(2.0, 2.0, 0.1), 2.0)

    def test_monotone_convergence(self):
        ev, prev = -2.0, -2.0
        for _ in range(400):
            ev = ex.adapt(ev, 3.0, 1 / 60)
            self.assertGreaterEqual(ev, prev)
            self.assertLessEqual(ev, 3.0)
            prev = ev
        self.assertAlmostEqual(ev, 3.0, delta=1e-6)

    def test_dt_subdivision_invariance(self):
        for start, target in ((0.0, 2.0), (2.0, -1.0)):
            one = ex.adapt(start, target, 0.032)
            two = ex.adapt(ex.adapt(start, target, 0.016), target, 0.016)
            self.assertAlmostEqual(one, two, delta=1e-6)

    def test_dt_clamp(self):
        self.assertEqual(ex.clamp_dt(3.0), ex.DT_MAX)
        self.assertEqual(ex.clamp_dt(0.0), ex.DT_MIN)
        self.assertEqual(ex.clamp_dt(-1.0), ex.DT_MIN)
        self.assertEqual(ex.clamp_dt(float('inf')), ex.DT_MAX)
        self.assertEqual(ex.clamp_dt(1 / 60), 1 / 60)
        # A 3 s hitch steps exactly as a 0.2 s frame would.
        self.assertEqual(ex.adapt(0.0, 4.0, 3.0), ex.adapt(0.0, 4.0, 0.2))
        self.assertLess(ex.adapt(0.0, 4.0, 3.0), 4.0 * 0.4)

    def test_ev_clamps_hold(self):
        self.assertEqual(ex.adapt(7.9, 20.0, 0.2, ev_max=8.0), 8.0)
        self.assertEqual(ex.adapt(-7.9, -20.0, 0.2, ev_min=-8.0), -8.0)
        self.assertLessEqual(ex.adapt(ex.EV_MAX, ex.EV_MAX, 0.1), ex.EV_MAX)
        with self.assertRaises(ValueError):
            ex.adapt_rate(0.1, 0.0)

    def test_settles_within_two_seconds(self):
        # Sector change: 3 EV brighter target at 60 Hz. Acceptance (d) of §7: settled in 2 s, no overshoot.
        frames = 120
        consumed = ex.simulate([math.log2(0.18) - 3.0] * (frames + 1), [1 / 60] * (frames + 1))
        self.assertEqual(consumed[0], 0.0)  # frame 0 consumes the initial state (lag of one frame)
        self.assertLess(abs(consumed[frames] - 3.0), 3.0 * math.exp(-5) + 1e-3)  # 5 tau_up: 0.7 % of the step
        self.assertTrue(all(b >= a for a, b in zip(consumed, consumed[1:])))
        self.assertTrue(all(c <= 3.0 for c in consumed))

    def test_simulation_is_deterministic(self):
        logs = [math.log2(0.05 + 0.01 * (i % 7)) for i in range(50)]
        dts = [1 / 60 + 0.001 * (i % 3) for i in range(50)]
        self.assertEqual(ex.simulate(logs, dts), ex.simulate(logs, dts))
        with self.assertRaises(ValueError):
            ex.simulate(logs, dts[:-1])


class OverrideTests(unittest.TestCase):
    def test_manual_override(self):
        for adapted in (-3.0, 0.0, 5.5):
            self.assertEqual(ex.resolve_ev('manual', 1.5, adapted), 1.5)
            self.assertEqual(ex.resolve_ev('auto', 1.5, adapted), adapted)
        self.assertEqual(ex.exposure_multiplier(ex.resolve_ev('manual', 2.0, 0.0)), 4.0)
        with self.assertRaises(ValueError):
            ex.resolve_ev('manual', float('nan'), 0.0)
        with self.assertRaises(ValueError):
            ex.resolve_ev('fixed', 0.0, 0.0)

    def test_exposure_multiplier(self):
        self.assertEqual(ex.exposure_multiplier(0.0), 1.0)
        self.assertEqual(ex.exposure_multiplier(-1.0), 0.5)
        self.assertEqual(ex.exposure_multiplier(3.0), 8.0)


class MeterTests(unittest.TestCase):
    def test_level0_clamps(self):
        self.assertEqual(ex.meter_level0((0.0, 0.0, 0.0), 'none'), math.log2(ex.METER_FLOOR))
        self.assertEqual(ex.meter_level0((1e6, 1e6, 1e6), 'none'), math.log2(ex.METER_CLIP))
        self.assertAlmostEqual(ex.meter_level0((0.5, 0.5, 0.5), 'none'), -1.0)
        self.assertAlmostEqual(ex.meter_level0((0.5, 0.5, 0.5), 'gamma2.2'), 2.2 * -1.0)
        self.assertTrue(ex.meter_clipped((100.0,) * 3, 'none'))
        self.assertFalse(ex.meter_clipped((1.0,) * 3, 'none'))

    def test_sun_disc_bounded_by_clip(self):
        # 1% of the pixels are a 1e6 sun over a 0.05 background: the clip bounds the
        # geometric-mean shift to 1% of log2(64 / 0.05) instead of 1% of log2(1e6 / 0.05).
        background = [(0.05,) * 3] * 990
        sun = [(1e6,) * 3] * 10
        metered = ex.meter_image(background + sun, 'none')
        shift = metered['avg_log_l'] - math.log2(0.05)
        self.assertAlmostEqual(shift, 0.01 * math.log2(64 / 0.05), places=9)
        self.assertLess(shift, 0.11)
        self.assertEqual(metered['meter_clipped_fraction'], 0.01)
        self.assertAlmostEqual(ex.ev_target(metered['avg_log_l']), math.log2(0.18 / 0.05) - shift)

    def test_chain_reduces_exactly_on_power_of_four_sizes(self):
        w = h = 16
        level = [math.log2(0.01 + (i % 13) * 0.05) for i in range(w * h)]
        self.assertAlmostEqual(ex.reduce_chain(level, w, h), ex.reduce_mean(level), places=12)
        self.assertAlmostEqual(ex.reduce_chain([2.0], 1, 1), 2.0)

    def test_chain_handles_odd_sizes_with_edge_clamp(self):
        w, h = 5, 3
        level = [float(i) for i in range(w * h)]
        chained = ex.reduce_chain(level, w, h)
        exact = ex.reduce_mean(level)
        self.assertNotAlmostEqual(chained, exact, places=6)
        self.assertLess(abs(chained - exact), 3.0)  # edge duplication, not a scale error
        self.assertEqual(ex.reduce_chain([1.5] * (w * h), w, h), 1.5)
        with self.assertRaises(ValueError):
            ex.reduce_chain(level, 4, 4)
        with self.assertRaises(ValueError):
            ex.reduce_mean([])


class TaaWeightTests(unittest.TestCase):
    def test_k_identity_and_round_trip(self):
        rgb = (4.0, 1.0, 0.25)
        self.assertEqual(ex.weight_color(rgb, ex.taa_k(0.0)), rgb)
        for k in (0.5, 1.0, 8.0):
            weighted = ex.weight_color(rgb, k)
            self.assertLess(weighted[0], rgb[0])
            for a, b in zip(ex.unweight_color(weighted, k), rgb):
                self.assertAlmostEqual(a, b, places=12)
        self.assertAlmostEqual(ex.luma_weight(1.0, 1.0), 0.5)
        with self.assertRaises(ValueError):
            ex.taa_k(-1.0)


if __name__ == '__main__':
    unittest.main()
