import importlib.util
import unittest
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "fog_prefix_replay", ROOT / "tools/analysis/fog_prefix_replay.py")
replay = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(replay)


class FogPrefixReplay(unittest.TestCase):
    def test_fixed_pixel_lattices_are_independent(self):
        coarse_x, coarse_y = replay.pixel_grid(64, 36)
        hold_x, hold_y = replay.pixel_grid(128, 72)
        np.testing.assert_array_equal(np.unique(coarse_x)[:4], [10, 30, 50, 70])
        np.testing.assert_array_equal(np.unique(hold_x)[:4], [5, 15, 25, 35])
        np.testing.assert_array_equal(np.unique(coarse_y)[:4], [10, 32, 53, 74])
        np.testing.assert_array_equal(np.unique(hold_y)[:4], [5, 16, 26, 37])
        self.assertEqual(len(coarse_x), 2304)
        self.assertEqual(len(hold_x), 9216)
        self.assertEqual(np.intersect1d(np.unique(coarse_x), np.unique(hold_x)).size, 0)

    def test_angular_bilinear_uses_actual_knots_and_clamps_edges(self):
        knot_x = np.array([10, 30, 50]); knot_y = np.array([10, 32])
        x = np.array([20, 5, 55]); y = np.array([21, 5, 40])
        indices, weights = replay.angular_neighbors(x, y, knot_x, knot_y)
        np.testing.assert_allclose(weights.sum(axis=1), 1, rtol=0, atol=1e-7)
        np.testing.assert_allclose(weights[0], [.25, .25, .25, .25], rtol=0, atol=1e-7)
        np.testing.assert_array_equal(indices[1], [0, 0, 0, 0])
        np.testing.assert_array_equal(indices[2], [5, 5, 5, 5])

    def test_depth_reconstruction_uses_exponential_segment_fraction(self):
        tau = 2.0
        prefix = {"S": np.zeros((33, 1, 3), np.float32), "T": np.ones((33, 1), np.float32)}
        prefix["S"][1:] = [.8, .4, .2]; prefix["T"][1:] = np.exp(-tau)
        neighbors = np.zeros((3, 4), np.int32); weights = np.zeros((3, 4), np.float32); weights[:, 0] = 1
        limits = np.array([replay.fog.NEAR, replay.fog.NEAR + replay.PREFIX_WIDTH*.5,
                           replay.fog.NEAR + replay.PREFIX_WIDTH])
        out = replay.depth_reconstruct(prefix, limits, neighbors, weights)
        expected_factor = (1-np.exp(-tau*.5))/(1-np.exp(-tau))
        np.testing.assert_allclose(out["S"][1], np.array([.8, .4, .2])*expected_factor, rtol=0, atol=2e-7)
        self.assertAlmostEqual(float(out["T"][1]), float(np.exp(-tau*.5)), places=7)
        np.testing.assert_array_equal(out["S"][[0, 2]], np.array([[0, 0, 0], [.8, .4, .2]], np.float32))

    def test_vanishing_optical_depth_uses_linear_limit(self):
        prefix = {"S": np.zeros((33, 1, 3), np.float32), "T": np.ones((33, 1), np.float32)}
        prefix["S"][1:] = [.6, .3, .15]
        neighbors = np.zeros((1, 4), np.int32); weights = np.array([[1, 0, 0, 0]], np.float32)
        out = replay.depth_reconstruct(prefix, np.array([replay.fog.NEAR + replay.PREFIX_WIDTH*.25]), neighbors, weights)
        np.testing.assert_allclose(out["S"][0], [.15, .075, .0375], rtol=0, atol=1e-8)
        self.assertEqual(float(out["T"][0]), 1.)

    def test_angular_stage_interpolates_S_and_T_directly(self):
        prefix = {"S": np.zeros((33, 2, 3), np.float32), "T": np.ones((33, 2), np.float32)}
        prefix["T"][1:, 0] = .25
        prefix["S"][1:, 0] = [1, 0, 0]; prefix["S"][1:, 1] = [0, 1, 0]
        neighbors = np.array([[0, 1, 0, 1]], np.int32); weights = np.array([[.5, .5, 0, 0]], np.float32)
        out = replay.depth_reconstruct(prefix, np.array([replay.fog.NEAR + replay.PREFIX_WIDTH]), neighbors, weights)
        np.testing.assert_allclose(out["S"][0], [.5, .5, 0], rtol=0, atol=0)
        self.assertEqual(float(out["T"][0]), .625)  # arithmetic blend, not geometric mean .5

    def test_boundary_mask_uses_holdout_geometry_and_raw_depth(self):
        geometry = np.array([[False, False, False, False], [False, False, False, True],
                             [True, True, True, True]])
        depth = np.array([[0, 0, 0, 0], [0, 0, 0, 100], [100, 104, 120, 120]], np.float32)
        mask = replay.boundary_mask(geometry.ravel(), depth.ravel(), shape=(3, 4)).reshape(3, 4)
        # Sky/geometry transitions and the 104->120 jump (>5% of 104) mark both sides.
        self.assertTrue(mask[1, 2] and mask[1, 3])
        self.assertTrue(mask[2, 1] and mask[2, 2])
        self.assertFalse(mask[0, 0])

    def test_short_limit_far_identity_and_source_alpha(self):
        prefix = {"S": np.ones((2, 1, 3), np.float32), "T": np.full((2, 1), .5, np.float32)}
        prefix["S"][0] = 0; prefix["T"][0] = 1
        neighbors = np.zeros((2, 4), np.int32); weights = np.zeros((2, 4), np.float32); weights[:, 0] = 1
        far = replay.depth_reconstruct(prefix, np.array([0., replay.fog.NEAR]), neighbors, weights)
        np.testing.assert_array_equal(far["S"], np.zeros((2, 3), np.float32))
        np.testing.assert_array_equal(far["T"], np.ones(2, np.float32))
        source = np.array([[.1, .2, .3, 0.], [.7, .8, .9, .73]], np.float32)
        result = replay.composite_over_source(source, far)
        np.testing.assert_array_equal(result[:, 3], source[:, 3])

    def test_constant_and_vacuum_operator_laws(self):
        laws = replay.constant_vacuum_laws()
        self.assertTrue(all(laws.values()), laws)


if __name__ == "__main__":
    unittest.main()
