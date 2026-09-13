"""Numerical checks of the offline study only; no production policy change."""
import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
try:
    import numpy as np
except ModuleNotFoundError as error:
    if error.name != 'numpy':
        raise
    np = None
if np is not None:
    import evaluate_space_exposure as study


@unittest.skipIf(np is None, 'offline exposure study requires NumPy')
class SpaceExposureStudyTests(unittest.TestCase):
    def test_reduction_matches_scalar_with_odd_edges(self):
        # Width 513 forces two reductions: 513 -> 129 -> 33.
        for width in (9, 513):
            with self.subTest(width=width):
                logs = np.arange(7 * width, dtype=float).reshape(7, width) / 5 - 10
                means, maxima = study.reduce_tiles(logs)
                expected = study.reference.reduce_tiles(logs.ravel().tolist(), width, 7)
                np.testing.assert_allclose(means.ravel(), expected[0], atol=1e-14)
                np.testing.assert_array_equal(maxima.ravel(), expected[1])
                self.assertEqual(means.shape, (expected[3], expected[2]))

    def test_agx_matches_independent_scalar(self):
        colors = np.array([[0., 0., 0.], [.01, .05, .08], [.18, .18, .18], [1., .5, 4.], [64., .001, .2]])
        for ev in (-.5, 0., .305, 2.):
            expected = np.array([study.agx.agx(c, exposure_ev=ev) for c in colors])
            np.testing.assert_allclose(study.display_rgb(colors, ev), expected, atol=1e-12)

    def test_synthetic_counterexamples(self):
        rows = {row['name']: row for row in study.synthetic_rows()}
        self.assertEqual(rows['dim_nebula']['targets']['current'], 2)
        self.assertEqual(rows['dim_nebula']['targets']['broad_highlight'], 0)
        self.assertTrue(all(target == 0 for target in rows['black']['targets'].values()))
        self.assertTrue(all(row['targets']['fixed0'] == 0 for row in rows.values()))
        self.assertEqual(rows['lit_0.9_percent']['targets']['current'], 0)
        self.assertEqual(rows['lit_1.1_percent']['targets']['current'], 2)
        self.assertEqual(rows['lit_1.1_percent']['targets']['restrained'], 0)
        self.assertLess(rows['one_star_per_tile']['targets']['current'], -2)
        self.assertEqual(rows['one_star_per_tile']['targets']['broad_highlight'], 0)
        self.assertEqual(rows['small_flash']['targets']['broad_highlight'], 0)
        self.assertEqual(rows['broad_white_planet']['targets']['broad_highlight'], -.5)

    def test_restrained_confidence_is_continuous_at_gate(self):
        base = dict(lit_fraction=.01, lit_median_log=math.log2(.003), p99_max_log=math.log2(.5))
        targets = [study.restrained_target(dict(base, lit_fraction=x)) for x in (.009999, .01, .010001)]
        self.assertLess(max(targets)-min(targets), 1e-8)


if __name__ == '__main__':
    unittest.main()
