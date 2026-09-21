import importlib.util
import unittest
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "taa_lattice_gate_replay", ROOT / "tools/analysis/taa_lattice_gate_replay.py")
gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gate)


def brute_box(a, radius, shape):
    """Window extrema by direct fetch, the definition the separable pass must match."""
    n = 2 * radius + 1
    h, w = shape
    stack = np.array([a[oy:oy + h, ox:ox + w] for oy in range(n) for ox in range(n)])
    return stack.min(0), stack.max(0)


class BoxMinMax(unittest.TestCase):
    def test_separable_box_equals_direct_fetch(self):
        rng = np.random.default_rng(7)
        for radius in (3, 5):
            a = rng.normal(size=(24 + 2 * radius, 31 + 2 * radius, 3))
            lo, hi = gate.box_minmax(a, radius)
            blo, bhi = brute_box(a, radius, (24, 31))
            self.assertEqual(lo.shape, (24, 31, 3))
            np.testing.assert_array_equal(lo, blo)
            np.testing.assert_array_equal(hi, bhi)

    def test_box_brackets_the_centre_sample(self):
        rng = np.random.default_rng(11)
        a = rng.normal(size=(20, 20, 3))
        lo, hi = gate.box_minmax(a, 3)
        centre = a[3:17, 3:17]
        self.assertTrue((lo <= centre + 1e-12).all())
        self.assertTrue((hi >= centre - 1e-12).all())

    def test_wide_box_clamps_at_the_frame_border(self):
        rng = np.random.default_rng(13)
        cur = rng.uniform(0, 4, size=(40, 48, 4))
        ys, xs = np.mgrid[0:9, 0:9]  # crop flush against the top-left frame corner
        weigh = lambda c, k: c  # identity: the clip's domain is not what this test covers
        lo, hi = gate.wide_box(cur, ys, xs, 3, weigh, 0.0)
        ry = np.clip(np.arange(-3, 9 + 3), 0, 39)
        rx = np.clip(np.arange(-3, 9 + 3), 0, 47)
        blo, bhi = brute_box(cur[np.ix_(ry, rx)][..., :3], 3, (9, 9))
        np.testing.assert_array_equal(lo, blo)
        np.testing.assert_array_equal(hi, bhi)


class VariantDefinition(unittest.TestCase):
    def test_patches_still_match_the_resolve_oracle_exactly_once(self):
        source = (ROOT / "tools/analysis/taa_resolve_replay.py").read_text()
        for old, _ in gate.PATCHES:
            self.assertEqual(source.count(old), 1, old)

    def test_variants_are_the_four_ratified_configurations(self):
        self.assertEqual(list(gate.VARIANTS),
                         ['installed', 'gate_open', 'gate_open_box7', 'gate_open_box11'])
        for name, opt in gate.VARIANTS.items():
            self.assertEqual(opt['thinregion'], {'W': 0.97, 'lo': 0.030, 'hi': 0.250})
            self.assertNotIn('history_kernel', opt)  # plain Catmull-Rom everywhere
            self.assertIs(opt.get('coherent', False), name != 'installed')
        self.assertNotIn('widebox', gate.VARIANTS['gate_open'])
        self.assertEqual(gate.VARIANTS['gate_open_box7']['widebox'], 3)
        self.assertEqual(gate.VARIANTS['gate_open_box11']['widebox'], 5)

    def test_sequences_cover_the_two_controls_and_the_trail_case(self):
        roles = {n: s['role'] for n, s in gate.SEQUENCES.items()}
        self.assertEqual(roles['run177-stationary'], 'control')
        self.assertEqual(roles['run159-slow'], 'control')
        self.assertEqual(roles['run161-trail'], 'trail')
        self.assertEqual(sum(r == 'rotation' for r in roles.values()), 5)
        for spec in gate.SEQUENCES.values():
            x0, y0, x1, y1 = spec['roi']
            tx0, ty0, tx1, ty1 = spec['track']
            self.assertTrue(x0 < tx0 < tx1 < x1 and y0 < ty0 < ty1 < y1)


class Verdicts(unittest.TestCase):
    def summary(self, rms, gradient, stale, trail, identical):
        def variant(name):
            return {'tracked_rms_ratio': rms[name], 'tracked_gradient_ratio': gradient[name],
                    'background_trail_p99_vs_plain': trail[name],
                    'identical_to_installed': identical[name]}
        variants = {n: variant(n) for n in gate.VARIANTS}
        return {'acceptance': {'tracked_rms_ratio_max': 0.80, 'tracked_gradient_ratio_min': 0.90,
                               'stale_patch_codes_max': 94.0},
                'sequences': {
                    'run177-rotation': {'role': 'rotation', 'variants': variants,
                                        'stale_patch': {'by_variant': {
                                            n: {'added_codes_at_site': stale[n]} for n in stale}}},
                    'run161-trail': {'role': 'trail', 'variants': variants},
                    'run177-stationary': {'role': 'control', 'variants': variants}}}

    def test_every_criterion_must_hold_for_a_pass(self):
        good = dict.fromkeys(gate.VARIANTS, 0.5)
        grad = dict.fromkeys(gate.VARIANTS, 0.95)
        stale = dict.fromkeys(gate.VARIANTS, 50.0)
        trail = dict.fromkeys(gate.VARIANTS, 0.1)
        same = dict.fromkeys(gate.VARIANTS, True)
        base = gate.verdicts(self.summary(good, grad, stale, trail, same))
        self.assertTrue(all(base[k]['pass'] for k in gate.VARIANTS))
        for field, table, value in (('tracked_rms', good, 0.81),
                                    ('tracked_gradient', grad, 0.89),
                                    ('controls_bit_identical', same, False)):
            broken = dict(table)
            broken['gate_open_box7'] = value
            args = [good, grad, stale, trail, same]
            args[[good, grad, stale, trail, same].index(table)] = broken
            got = gate.verdicts(self.summary(*args))
            self.assertFalse(got['gate_open_box7'][field], field)
            self.assertFalse(got['gate_open_box7']['pass'], field)
            self.assertTrue(got['gate_open']['pass'], field)

    def test_stale_patch_is_reported_but_does_not_gate_the_pass(self):
        stale = dict.fromkeys(gate.VARIANTS, 50.0)
        stale['gate_open'] = 240.0
        got = gate.verdicts(self.summary(dict.fromkeys(gate.VARIANTS, 0.5),
                                         dict.fromkeys(gate.VARIANTS, 0.95), stale,
                                         dict.fromkeys(gate.VARIANTS, 0.1),
                                         dict.fromkeys(gate.VARIANTS, True)))
        self.assertEqual(got['gate_open']['stale_patch_codes'], 240.0)
        self.assertFalse(got['gate_open']['stale_patch_within_2x_clipped'])
        self.assertTrue(got['gate_open']['pass'])

    def test_variants_are_ranked_by_tracked_rms_gain(self):
        rms = {'installed': 1.0, 'gate_open': 0.4, 'gate_open_box7': 0.6, 'gate_open_box11': 0.5}
        got = gate.verdicts(self.summary(rms, dict.fromkeys(gate.VARIANTS, 0.95),
                                         dict.fromkeys(gate.VARIANTS, 50.0),
                                         dict.fromkeys(gate.VARIANTS, 0.1),
                                         dict.fromkeys(gate.VARIANTS, True)))
        self.assertEqual(got['_ranking_by_tracked_rms_gain'],
                         ['gate_open', 'gate_open_box11', 'gate_open_box7', 'installed'])

    def test_trail_is_measured_against_the_installed_variant(self):
        trail = dict.fromkeys(gate.VARIANTS, 0.1)
        trail['gate_open'] = 0.2
        got = gate.verdicts(self.summary(dict.fromkeys(gate.VARIANTS, 0.5),
                                         dict.fromkeys(gate.VARIANTS, 0.95),
                                         dict.fromkeys(gate.VARIANTS, 50.0), trail,
                                         dict.fromkeys(gate.VARIANTS, True)))
        self.assertFalse(got['gate_open']['run161_trail'])
        self.assertTrue(got['gate_open_box7']['run161_trail'])


class Tracking(unittest.TestCase):
    def test_bilinear_reproduces_a_linear_ramp(self):
        a = np.arange(6)[:, None] * 10.0 + np.arange(7)[None, :]
        x = np.array([0.0, 2.5, 5.75])
        y = np.array([1.0, 3.25, 4.5])
        np.testing.assert_allclose(gate.bilinear(a, x, y), y * 10 + x, atol=1e-9)

    def test_erode_removes_a_border_of_the_requested_radius(self):
        mask = np.zeros((11, 11), bool)
        mask[2:9, 3:8] = True
        self.assertEqual(gate.erode(mask, 1).sum(), 5 * 3)
        self.assertEqual(gate.erode(mask, 3).sum(), 0)


if __name__ == '__main__':
    unittest.main()
