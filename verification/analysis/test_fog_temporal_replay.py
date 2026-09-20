import importlib.util
from pathlib import Path
import tempfile
import unittest
import json
import hashlib

import numpy as np


ROOT = Path(__file__).parents[2]
SPEC = importlib.util.spec_from_file_location(
    "fog_temporal_replay", ROOT / "tools/analysis/fog_temporal_replay.py")
fog = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(fog)


class FogTemporalReplayTests(unittest.TestCase):
    def test_exact_reduced_point_indices(self):
        self.assertEqual(fog.centre_indices(1280, 120).tolist()[:5], [0, 10, 21, 32, 42])
        self.assertEqual(fog.centre_indices(768, 72).tolist()[-3:], [736, 746, 757])
        with self.assertRaises(ValueError):
            fog.centre_indices(0, 72)

    def test_loads_existing_resolve_and_identity_history_is_finite(self):
        width, height = 24, 18
        crop = (3, 3, width - 3, height - 3)
        resolve, source_hash = fog.load_existing_resolve(
            ROOT / "tools/analysis/taa_resolve_replay.py", width, height, crop)
        current = np.full((height, width, 4), np.float32(.25)); current[..., 3] = 1
        depth = np.full((height, width), np.float32(-1))
        motion = np.zeros((height, width, 4), np.float32); motion[..., 3] = -1
        age = np.ones((crop[3] - crop[1], crop[2] - crop[0]))
        rotation = np.eye(3)
        output, _, diagnostic = resolve(current, depth, motion, current.copy(), depth.copy(), age,
                                        (0., 0.), 2., .9, rotation, rotation,
                                        (.8, 1.333333, 0., 0.), 1, {})
        self.assertEqual(len(source_hash), 64)
        self.assertTrue(np.isfinite(output).all())
        self.assertTrue(np.array_equal(output, current[crop[1]:crop[3], crop[0]:crop[2]].astype("<f2")))
        self.assertTrue(diagnostic["far"].all())

    def test_rotation_history_ignores_translation(self):
        rotation = np.eye(3)
        first = fog.rotation_history_coordinates(20, 12, (3, 3, 17, 9), (0., 0.),
                                                 (.8, 1.333333, 0., 0.), rotation, rotation)
        second = fog.rotation_history_coordinates(20, 12, (3, 3, 17, 9), (0., 0.),
                                                  (.8, 1.333333, 0., 0.), rotation, rotation)
        for a, b in zip(first, second):
            self.assertTrue(np.array_equal(a, b))
        _, origin0 = fog.camera({"camera_state": {
            **{f"r{i}{j}": str(float(i == j)) for i in range(3) for j in range(3)}, "t": "0,0,0"}})
        _, origin1 = fog.camera({"camera_state": {
            **{f"r{i}{j}": str(float(i == j)) for i in range(3) for j in range(3)}, "t": "-81,0,0"}})
        self.assertEqual(float(np.linalg.norm(origin1 - origin0)), 81.)

    def test_metrics_are_json_finite(self):
        self.assertEqual(fog.metrics(np.array([]))["count"], 0)
        row = fog.metrics(np.array([0., 1., np.nan]))
        self.assertEqual(row["count"], 2)
        self.assertEqual(row["max"], 1.)
        self.assertEqual("{frame}-v0.composite.rgba16f".format(frame=1974),
                         "1974-v0.composite.rgba16f")

    def test_incremental_fog_control_cancels_identical_branches(self):
        image = np.full((4, 5, 4), np.float32(.5))
        image[..., 3] = 1
        self.assertTrue(np.all(fog.display_codes(image, 2.) - fog.display_codes(image, 2.) == 0))

    def test_production_report_binding_requires_inputs_cases_and_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); manifest_sha = "a" * 64; frames = [1974, 1975]
            inputs = {"sequence_manifest": {"sha256": manifest_sha}}
            payload = (json.dumps(inputs) + "\n").encode()
            (root / "inputs.json").write_bytes(payload)
            report = {
                "passed": True,
                "inputs_sha256": hashlib.sha256(payload).hexdigest(),
                "numerical": {"passed": True, "cases": [
                    {"frame": frame, "variant": 0, "passed": True} for frame in frames],
                    "readback_hashes": {
                        f"{frame}-v0.composite.rgba16f": str(frame).zfill(64) for frame in frames}},
            }
            kind, hashes = fog.validate_gpu_report(
                report, root / "report.json", manifest_sha, frames,
                "{frame}-v0.composite.rgba16f")
            self.assertEqual(kind, "production")
            self.assertEqual(len(hashes), 2)
            label, limits, next_step = fog.output_contract(kind)
            self.assertIn("actual production", label)
            self.assertFalse(any("not actual production" in row for row in limits))
            self.assertIn("substitution is complete", next_step)
            report["numerical"]["cases"][0]["passed"] = False
            with self.assertRaisesRegex(ValueError, "missing or failed"):
                fog.validate_gpu_report(report, root / "report.json", manifest_sha, frames,
                                        "{frame}-v0.composite.rgba16f")

    def test_temporal_metadata_rejects_nonfinite_and_invalid_ranges(self):
        metadata = {"motion_output_frame": {"jitter_x": "0", "jitter_y": "0",
                                             "taa_k": "2", "taa_weight": ".9"},
                    "camera_state": {"p00": ".8", "p11": "1.333333",
                                     "p20": "0", "p21": "0"}}
        jitter, projection, k, weight = fog.temporal_parameters(metadata, 120, 72)
        self.assertEqual((jitter, projection, k, weight), ((0., 0.), (.8, 1.333333, 0., 0.), 2., .9))
        for group, key, value in (("motion_output_frame", "jitter_x", "nan"),
                                  ("camera_state", "p00", "0"),
                                  ("motion_output_frame", "taa_k", "-1"),
                                  ("motion_output_frame", "taa_weight", "1.1")):
            broken = {name: dict(row) for name, row in metadata.items()}
            broken[group][key] = value
            with self.assertRaises(ValueError):
                fog.temporal_parameters(broken, 120, 72)


if __name__ == "__main__":
    unittest.main()
