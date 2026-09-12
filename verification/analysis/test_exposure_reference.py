"""Exposure model host reference (design §3 and the stage-2 space-aware meter): metering, the tile
statistic and target, adaptation, clamps, override, TAA weighting."""
import math
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import exposure_reference as ex  # noqa: E402


def uniform(log_l):
    return ex.uniform_statistics(log_l)


class TargetTests(unittest.TestCase):
    def test_key_maps_to_zero_ev(self):
        self.assertAlmostEqual(ex.ev_target(uniform(math.log2(0.18))), 0.0)
        self.assertAlmostEqual(ex.ev_target(uniform(math.log2(0.045))), 2.0)
        self.assertAlmostEqual(ex.ev_target(uniform(math.log2(0.18)), ev_offset=-1.0), -1.0)
        self.assertAlmostEqual(ex.ev_target(uniform(math.log2(0.36)), key=0.36), 0.0)

    def test_target_clamps(self):
        self.assertEqual(ex.ev_target(uniform(-8.0)), ex.EV_MAX)
        self.assertEqual(ex.ev_target(uniform(-13.0)), 0.0)   # below the background floor: nothing lit, neutral
        self.assertAlmostEqual(ex.ev_target(uniform(6.0)), ex.ev_limit(6.0))            # at the clip the limit (-2.13) wins over the pulled key rule (-2.12)
        self.assertEqual(ex.ev_target(uniform(6.0), key_pull=1.0), ex.EV_MIN)          # the full key rule asks for -8.5: the clamp
        self.assertEqual(ex.ev_target(uniform(6.0), ev_min=-1.0), -1.0)
        self.assertEqual(ex.ev_target(uniform(0.0), ev_min=-1.0, ev_max=1.0, key_pull=1.0), -1.0)
        self.assertEqual((ex.EV_MIN, ex.EV_MAX), (-3.0, 2.0))
        with self.assertRaises(ValueError):
            ex.ev_key(0.0, key=0.0)
        with self.assertRaises(ValueError):
            ex.ev_key(float('nan'))

    def test_key_pull_is_gentle_and_continuous(self):
        # Brighter than the key: only key_pull of the rule; darker: the full lift; equal: 0 either way.
        self.assertAlmostEqual(ex.ev_key(0.0), ex.KEY_PULL * math.log2(ex.KEY))
        self.assertAlmostEqual(ex.ev_key(math.log2(ex.KEY)), 0.0)
        self.assertAlmostEqual(ex.ev_key(math.log2(ex.KEY) - 1.0), 1.0)
        self.assertAlmostEqual(ex.ev_key(0.0, key_pull=1.0), math.log2(ex.KEY))
        self.assertAlmostEqual(ex.ev_key(0.0, key_pull=0.0), 0.0)
        self.assertAlmostEqual(ex.ev_key(0.0, ev_offset=0.5), ex.KEY_PULL * math.log2(ex.KEY) + 0.5)

    def test_highlight_limit(self):
        # The brightest 1 % of tiles reach white_target of the AgX white at the limit.
        for p99 in (-2.0, 0.0, 3.0, 6.0):
            self.assertAlmostEqual(ex.ev_limit(p99) + p99, math.log2(0.9 * ex.TONEMAP_WHITE))
        self.assertEqual(ex.ev_limit(6.0, white_target=0.0), ex.EV_MAX)
        self.assertAlmostEqual(ex.TONEMAP_WHITE, 16.2917424, places=6)
        clipped = ex.meter_statistics([-5.44] * 256, [-5.44] * 251 + [6.0] * 5)
        target = ex.exposure_target(clipped)
        self.assertEqual(target['ev_target'], target['ev_limit'])
        self.assertLess(target['ev_limit'], 0.0)
        self.assertGreater(target['ev_key'], target['ev_limit'])


class StatisticTests(unittest.TestCase):
    def test_black_sky_with_lit_patch(self):
        # 64x64 black frame, a 16x16 patch at engine 0.3: 16 of 256 tiles lit; the old
        # rule would ask for +8 EV, the key rule lifts the patch to the key only.
        pixels = [(0.0,) * 3] * 4096
        for y in range(8, 24):
            for x in range(8, 24):
                pixels[y * 64 + x] = (0.3,) * 3
        m = ex.meter_image(pixels, 64, 64)
        self.assertEqual((m['tile_width'], m['tile_height'], m['tiles'], m['lit']), (16, 16, 256, 16))
        self.assertFalse(m['neutral'])
        self.assertAlmostEqual(m['lit_median_log'], ex.meter_level0((0.3,) * 3))
        self.assertAlmostEqual(m['p99_max_log'], ex.meter_level0((0.3,) * 3))
        t = ex.exposure_target(m)
        self.assertAlmostEqual(t['ev_key'], math.log2(0.18) - ex.meter_level0((0.3,) * 3))
        self.assertAlmostEqual(t['ev_target'], t['ev_key'])
        self.assertLess(t['ev_target'], 2.0)
        old_rule = math.log2(0.18) - m['avg_log_l']
        self.assertGreater(old_rule, 8.0)

    def test_speck_is_neutral(self):
        pixels = [(0.0,) * 3] * 4096
        for y in range(4):
            for x in range(4):
                pixels[(20 + y) * 64 + 20 + x] = (0.5,) * 3
        m = ex.meter_image(pixels, 64, 64)
        self.assertEqual(m['lit'], 1)
        self.assertTrue(m['neutral'])
        self.assertEqual(ex.exposure_target(m)['ev_key'], 0.0)
        self.assertEqual(ex.exposure_target(m, ev_offset=-1.0)['ev_key'], -1.0)

    def test_menu_is_pulled_down_gently(self):
        m = ex.meter_image([(1.0,) * 3] * 4096, 64, 64)
        t = ex.exposure_target(m)
        self.assertAlmostEqual(t['ev_target'], ex.KEY_PULL * math.log2(0.18))
        self.assertGreater(t['ev_target'], -0.7)
        self.assertLess(t['ev_target'], 0.0)

    def test_sparks_engage_the_limit(self):
        # Mid-grey with 0.5 % super-bright pixels in five tiles (2 % of the tiles).
        pixels = [(0.18,) * 3] * 4096
        for tx, ty in ((1, 1), (6, 4), (5, 8), (4, 12), (14, 14)):
            for y in range(2):
                for x in range(2):
                    pixels[(ty * 4 + y) * 64 + tx * 4 + x] = (100.0,) * 3
        m = ex.meter_image(pixels, 64, 64)
        self.assertAlmostEqual(m['meter_clipped_fraction'], 20 / 4096)
        self.assertAlmostEqual(m['p99_max_log'], math.log2(ex.METER_CLIP))
        t = ex.exposure_target(m)
        self.assertEqual(t['ev_target'], t['ev_limit'])
        self.assertAlmostEqual(t['ev_limit'], math.log2(0.9 * ex.TONEMAP_WHITE) - 6.0)
        self.assertGreater(t['ev_key'], 2.0)

    def test_centre_weighting_keeps_the_centre_object(self):
        # A white emitter over the left half, a mid-grey (decoded 0.18) object of 32x32 in the centre,
        # black elsewhere: weighted, the object sets the target (EV 0); unweighted, the emitter (-0.62).
        pixels = [(0.0,) * 3] * 4096
        for y in range(64):
            for x in range(32):
                pixels[y * 64 + x] = (1.0,) * 3
        for y in range(16, 48):
            for x in range(16, 48):
                pixels[y * 64 + x] = (0.459,) * 3
        weighted = ex.meter_image(pixels, 64, 64)
        unweighted = ex.meter_image(pixels, 64, 64, edge_weight=1.0)
        self.assertEqual((weighted['lit'], unweighted['lit']), (160, 160))
        self.assertAlmostEqual(weighted['lit_median_log'], ex.meter_level0((0.459,) * 3))
        self.assertAlmostEqual(unweighted['lit_median_log'], 0.0)
        self.assertAlmostEqual(ex.exposure_target(weighted)['ev_target'], ex.ev_key(ex.meter_level0((0.459,) * 3)), delta=1e-9)
        self.assertLess(abs(ex.exposure_target(weighted)['ev_target']), 0.02)
        self.assertAlmostEqual(ex.exposure_target(unweighted)['ev_target'], ex.KEY_PULL * math.log2(0.18))
        self.assertEqual(weighted['p99_max_log'], unweighted['p99_max_log'])   # the limit is unweighted
        w = ex.tile_weights(16, 16)
        self.assertAlmostEqual(max(w), w[8 * 16 + 8]); self.assertLess(min(w), 0.36); self.assertGreater(min(w), 0.35 - 1e-9)
        self.assertEqual(ex.tile_weights(3, 3, 1.0), [1.0] * 9)

    def test_dead_band(self):
        self.assertEqual(ex.apply_deadband(None, 0.1), 0.1)
        self.assertEqual(ex.apply_deadband(1.0, 1.2), 1.0)
        self.assertEqual(ex.apply_deadband(1.0, 0.8), 1.0)
        self.assertEqual(ex.apply_deadband(1.0, 1.3), 1.3)
        self.assertEqual(ex.apply_deadband(1.0, 1.2, ev_deadband=0.0), 1.2)
        # A sequence of small changes holds the target; adaptation converges exactly to the held one.
        levels = [math.log2(0.18) - 1.0] * 20 + [math.log2(0.18) - 1.2] * 20 + [math.log2(0.18) - 0.85] * 20 + [math.log2(0.18) - 2.5] * 40
        targets = []
        consumed = ex.simulate(levels, [0.2] * len(levels), targets=targets)
        self.assertEqual(set(targets[:60]), {1.0})
        self.assertEqual(set(targets[60:]), {2.0})   # clamped at ev_max
        self.assertAlmostEqual(consumed[60], 1.0, delta=1e-4)
        self.assertGreater(consumed[-1], 1.9)

    def test_median_and_percentile_definitions(self):
        means = [float(i) for i in range(10)]
        m = ex.meter_statistics(means, means)
        self.assertEqual(m['lit_median_log'], 4.0)      # lower median: index (10 - 1) // 2
        self.assertEqual(m['p99_max_log'], 9.0)         # index 10 * 99 // 100 = 9
        m = ex.meter_statistics(means[:9], means[:9])
        self.assertEqual(m['lit_median_log'], 4.0)
        nan = ex.meter_statistics([float('nan')] + means[1:], means)
        self.assertEqual(nan['lit'], 9)                 # the NaN tile reads as the floor: background
        self.assertTrue(math.isfinite(nan['avg_log_l']))
        with self.assertRaises(ValueError):
            ex.meter_statistics([], [])


class AdaptationTests(unittest.TestCase):
    def test_time_constants(self):
        # After tau seconds a first-order filter has covered 1 - 1/e of the distance.
        target = 3.0
        for tau, tau_up, tau_down in ((0.4, 0.4, 1.2), (1.2, 0.4, 1.2)):
            ev = 0.0 if tau == tau_up else 6.0
            t = 0.0
            while t < tau - 1e-9:
                ev = ex.adapt(ev, target, 0.01, tau_up, tau_down, -8.0, 8.0)
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
            ev = ex.adapt(ev, 3.0, 1 / 60, ev_max=8.0)
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
        consumed = ex.simulate([math.log2(0.18) - 3.0] * (frames + 1), [1 / 60] * (frames + 1), ev_max=8.0)
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
        # geometric-mean shift to 1% of log2(64 / 0.05) instead of 1% of log2(1e6 / 0.05);
        # the tile statistic sees the sun in the tile maxima and engages the limit.
        pixels = [(0.05,) * 3] * 1024
        for i in range(10):
            pixels[i * 100] = (1e6,) * 3
        metered = ex.meter_image(pixels, 32, 32, 'none')
        shift = metered['avg_log_l'] - math.log2(0.05)
        self.assertAlmostEqual(shift, 10 / 1024 * math.log2(64 / 0.05), places=9)
        self.assertLess(shift, 0.11)
        self.assertEqual(metered['meter_clipped_fraction'], 10 / 1024)
        self.assertEqual(metered['p99_max_log'], math.log2(64.0))
        self.assertEqual(ex.exposure_target(metered)['ev_target'], ex.ev_limit(math.log2(64.0)))

    def test_chain_reduces_exactly_on_power_of_four_sizes(self):
        w = h = 16
        level = [math.log2(0.01 + (i % 13) * 0.05) for i in range(w * h)]
        self.assertAlmostEqual(ex.reduce_chain(level, w, h), ex.reduce_mean(level), places=12)
        self.assertAlmostEqual(ex.reduce_chain([2.0], 1, 1), 2.0)
        means, maxes, tw, th = ex.reduce_tiles(level, w, h, 4, 4)
        self.assertEqual((tw, th, len(means)), (4, 4, 16))
        self.assertAlmostEqual(ex.reduce_mean(means), ex.reduce_mean(level), places=12)
        self.assertEqual(max(maxes), max(level))
        self.assertEqual(ex.reduce_tiles(level, w, h, 4, 16)[2:], (4, 4))   # level 0 always folds once

    def test_tile_geometry_at_game_sizes(self):
        # 1280x768 -> 320x192 -> 80x48 tiles; 5120x1440 -> 1280x360 -> 320x90 -> 80x23.
        def geometry(w, h):
            levels = []
            while not levels or w > ex.TILE_MAX or h > ex.TILE_MAX:
                w, h = -(-w // 4), -(-h // 4)
                levels.append((w, h))
            return levels
        self.assertEqual(geometry(1280, 768), [(320, 192), (80, 48)])
        self.assertEqual(geometry(5120, 1440), [(1280, 360), (320, 90), (80, 23)])
        self.assertEqual(geometry(64, 64), [(16, 16)])

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
