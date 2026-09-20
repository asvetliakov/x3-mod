import importlib.util
import unittest
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "fog_distance_replay", ROOT / "tools/analysis/fog_distance_replay.py")
replay = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(replay)


class FogDistanceReplay(unittest.TestCase):
    def volume(self):
        z, y, x = np.mgrid[:8, :8, :8]
        rho = ((x + 2 * y + 3 * z) % 7 != 0).astype(np.float32) * .5
        return np.concatenate((rho[..., None] * np.array([.2, .5, .8], np.float32), rho[..., None]), axis=-1)

    def test_mips_preserve_premultiplied_mean(self):
        levels = replay.mip_pyramid(self.volume())
        self.assertEqual([a.shape[0] for a in levels], [8, 4, 2, 1])
        for level in levels:
            np.testing.assert_allclose(level.mean(axis=(0, 1, 2), dtype=np.float64),
                                       levels[0].mean(axis=(0, 1, 2), dtype=np.float64), rtol=0, atol=2e-8)
            self.assertTrue(np.all(level[..., :3] <= level[..., 3:4] * np.array([.2, .5, .8]) + 1e-7))

    def test_wrapped_trilinear_sampling_is_periodic(self):
        volume = self.volume()
        points = np.array([[0., 17., 29.], [replay.PERIOD - .01, 100., 200.]])
        np.testing.assert_array_equal(replay.sample_level(volume, points),
                                      replay.sample_level(volume, points + [replay.PERIOD, 0, 0]))
        left = replay.sample_level(volume, np.array([[-1e-3, 11., 13.]]))
        right = replay.sample_level(volume, np.array([[replay.PERIOD - 1e-3, 11., 13.]]))
        np.testing.assert_allclose(left, right, atol=2e-7)

    def test_candidate_preserves_exact_near_and_depth_laws(self):
        volume = self.volume(); levels = replay.mip_pyramid(volume)
        direction = np.array([[1., 0., 0.], [0., 1., 0.], [0., 0., 1.]], np.float32)
        origin = np.array([100., 200., 300.]); limit = np.array([0., 5000., 12000.], np.float32)
        result = replay.candidate(levels, origin, direction, limit, 3.75e-6)
        # Reproduce the production 24-midpoint near segment independently.
        ds = limit / 24; index = np.arange(24, dtype=np.float32)[:, None]
        distance = ds[None, :] * (index + .5)
        rgba = replay.sample_level(volume, origin + direction[None, :, :] * distance[..., None])
        S, T, tau = replay.integrate_samples(rgba, np.broadcast_to(ds, distance.shape), distance, 3.75e-6, direction, False)
        np.testing.assert_array_equal(result["near_S"], S)
        np.testing.assert_array_equal(result["near_T"], T)
        np.testing.assert_array_equal(result["near_tau"], tau)
        np.testing.assert_array_equal(result["S"], S)
        np.testing.assert_array_equal(result["T"], T)
        self.assertEqual(float(result["T"][0]), 1.)
        self.assertTrue(np.array_equal(result["S"][0], np.zeros(3, np.float32)))

    def test_far_bins_keep_fixed_width_lod_when_depth_truncates(self):
        levels = replay.mip_pyramid(self.volume()); direction = np.array([[1., 0., 0.]], np.float32)
        a = replay.candidate(levels, np.zeros(3), direction, np.array([15000.], np.float32), 3.75e-6, 24)
        b = replay.candidate(levels, np.zeros(3), direction, np.array([200000.], np.float32), 3.75e-6, 24)
        self.assertAlmostEqual(a["lod_target"], b["lod_target"], places=12)
        self.assertAlmostEqual(a["lod_target"], np.log2(((replay.FAR - replay.NEAR) / 24) / 256), places=6)

    def test_accurate_reference_composes_ordered_shell_transport(self):
        volume = np.empty((8, 8, 8, 4), np.float32)
        volume[..., :3] = [.1, .25, .4]; volume[..., 3] = .5
        direction = np.array([[1., 0., 0.], [0., 1., 0.]], np.float32)
        limit = np.array([replay.FAR, 9000.], np.float32); sun = np.array([0., 0., 1.])
        result = replay.accurate_reference(volume, np.zeros(3), direction, limit, 4e-6, 128., sun)
        np.testing.assert_allclose(result["T"],
                                   result["near_T"] * result["shell1_T"] * result["shell2_T"], rtol=0, atol=2e-7)
        np.testing.assert_allclose(result["tau"],
                                   result["near_tau"] + result["shell1_tau"] + result["shell2_tau"], rtol=0, atol=2e-7)
        np.testing.assert_allclose(result["S"], result["near_S"] + result["shell1_added_S"] +
                                   result["shell2_added_S"], rtol=0, atol=2e-7)
        self.assertEqual(float(result["shell1_tau"][1]), 0.)
        self.assertEqual(float(result["shell2_tau"][1]), 0.)
        self.assertAlmostEqual(float(result["near_T"][1]), float(np.exp(-4e-6 * .5 * 9000)), places=6)

    def test_visibility_window_endpoints_and_derivatives(self):
        x = np.array([30000., replay.WINDOW_START, replay.FAR, 210000.])
        w = 1 - replay.smoothstep(replay.WINDOW_START, replay.FAR, x)
        np.testing.assert_array_equal(w, [1, 1, 0, 0])
        eps = .01
        self.assertLess(abs((1 - replay.smoothstep(replay.WINDOW_START, replay.FAR, np.array([replay.WINDOW_START + eps]))[0] - 1) / eps), 1e-6)
        self.assertLess(abs((1 - replay.smoothstep(replay.WINDOW_START, replay.FAR, np.array([replay.FAR - eps]))[0]) / eps), 1e-6)

    def test_camera_origin_uses_inverse_not_transpose(self):
        row = {f"r{i}{j}": str(value) for i, line in enumerate(((2., 0., 0.), (0., 3., 0.), (0., 0., 4.))) for j, value in enumerate(line)}
        row["t"] = "10,12,20"
        rotation, origin = replay.camera(row)
        np.testing.assert_allclose(origin, [-5., -4., -5.])
        self.assertFalse(np.array_equal(origin, -np.array([10., 12., 20.]) @ rotation.T))

    def test_captured_sun_uses_production_selected_point_slot_one(self):
        meta = {"point_sun": {"source": "point", "poll": "ok", "directional": "1",
                              "dir0": "1,0,0", "dir1": "0,.6,.8"}}
        np.testing.assert_array_equal(replay.captured_sun(meta), np.array([0., .6, .8], np.float32))
        meta["point_sun"]["poll"] = "failed"
        with self.assertRaisesRegex(ValueError, "provenance"):
            replay.captured_sun(meta)

    def test_ray_reconstruction_matches_uploaded_jittered_projection(self):
        camera = {f"r{i}{j}": str(float(i == j)) for i in range(3) for j in range(3)}
        camera.update(t="0,0,0", p00="0.8", p11="1.333333", p20="0.02", p21="-0.03")
        motion = {"jitter_x": "0.375", "jitter_y": "-0.277778"}
        x = np.array([100., 900.]); y = np.array([200., 600.])
        depth = np.array([[2., 0., 1., 0.], [2., 0., 1., 0.]], np.float32)
        _, direction, limit, geometry = replay.reconstruct_rays(
            {"camera": camera, "motion": motion}, x, y, depth)
        jx, jy = float(motion["jitter_x"]), float(motion["jitter_y"])
        # Algebraic reduction of production m20/m21 at texel-centre UV.
        view = np.stack((((x - jx) / replay.FULL_WIDTH * 2 - 1 - .02) / .8,
                         (1 - (y - jy) / replay.FULL_HEIGHT * 2 + .03) / 1.333333,
                         np.ones(2)), axis=-1)
        expected = view / np.linalg.norm(view, axis=1)[:, None]
        np.testing.assert_allclose(direction, expected, rtol=0, atol=6e-8)
        np.testing.assert_array_equal(limit, np.full(2, replay.FAR, np.float32))
        self.assertFalse(geometry.any())
        wrong = np.stack((((x + .5 + jx) / replay.FULL_WIDTH * 2 - 1 - .02) / .8,
                          (1 - (y + .5 + jy) / replay.FULL_HEIGHT * 2 + .03) / 1.333333,
                          np.ones(2)), axis=-1)
        wrong /= np.linalg.norm(wrong, axis=1)[:, None]
        self.assertGreater(float(np.max(np.abs(expected - wrong))), 1e-4)

    def test_metrics_use_stated_gates(self):
        passing = {"T": {"p99": .001, "max": .003},
                   "S_normalized_lighting": [{"p99": .0005, "max": .002}] * 3}
        self.assertTrue(replay.numerical_pass(passing))
        passing["T"] = {"p99": .00101, "max": .002}
        self.assertFalse(replay.numerical_pass(passing))
        passing["T"] = {"p99": .001, "max": .003}
        passing["S_normalized_lighting"][1] = {"p99": .00051, "max": .001}
        self.assertFalse(replay.numerical_pass(passing))

    def test_split_shell_operation_counts_include_both_partial_rows(self):
        self.assertEqual(int(np.ceil((replay.WINDOW_START - replay.NEAR) / 128) +
                             np.ceil((replay.FAR - replay.WINDOW_START) / 128)), 1470)
        self.assertEqual(int(np.ceil((replay.WINDOW_START - replay.NEAR) / 64) +
                             np.ceil((replay.FAR - replay.WINDOW_START) / 64)), 2939)


if __name__ == "__main__":
    unittest.main()
