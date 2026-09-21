#!/usr/bin/env python3
"""Replay the existing TAA resolve over the bounded spatial-fog sequence.

This is a derived, reduced-resolution witness.  It consumes actual run185 camera,
depth, motion and scene captures plus separately generated fog composites.  It
does not synthesize motion or qualify game/native-Windows behavior.
"""
from __future__ import annotations

import argparse
import ast
import hashlib
import json
import math
from pathlib import Path

import numpy as np


FULL_WIDTH, FULL_HEIGHT = 1280, 768
BURSTS = ((1974, "bluewell"), (9204, "bluewell"),
          (21901, "foggreenoutlands"), (26447, "foggreenoutlands"))
LUMA = np.array([0.2126, 0.7152, 0.0722], np.float64)


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            h.update(block)
    return h.hexdigest()


def centre_indices(source_size: int, output_size: int) -> np.ndarray:
    if source_size <= 0 or output_size <= 0:
        raise ValueError("positive dimensions required")
    return np.minimum((np.arange(output_size) * source_size / output_size).astype(np.int64),
                      source_size - 1)


def metrics(values: np.ndarray) -> dict:
    finite = np.asarray(values, np.float64)
    finite = finite[np.isfinite(finite)]
    if not finite.size:
        return {"count": 0, "mean": None, "p95": None, "p99": None, "max": None}
    return {"count": int(finite.size), "mean": float(finite.mean()),
            "p95": float(np.percentile(finite, 95)),
            "p99": float(np.percentile(finite, 99)), "max": float(finite.max())}


def camera(metadata: dict) -> tuple[np.ndarray, np.ndarray]:
    row = metadata["camera_state"]
    rotation = np.array([[float(row[f"r{i}{j}"]) for j in range(3)]
                         for i in range(3)], np.float64)
    translation = np.fromstring(row["t"], sep=",", dtype=np.float64)
    if translation.size != 3 or not np.isfinite(rotation).all() or not np.isfinite(translation).all():
        raise ValueError("finite camera metadata required")
    return rotation, -translation @ np.linalg.inv(rotation)


def temporal_parameters(metadata: dict, width: int, height: int):
    motion = metadata["motion_output_frame"]
    camera_row = metadata["camera_state"]
    raw_jitter = np.array([float(motion["jitter_x"]), float(motion["jitter_y"])])
    projection = np.array([float(camera_row[key]) for key in ("p00", "p11", "p20", "p21")])
    k = float(motion["taa_k"]); weight = float(motion["taa_weight"])
    values = np.concatenate([raw_jitter, projection, [k, weight]])
    if not np.isfinite(values).all():
        raise ValueError("finite TAA metadata required")
    if np.any(np.abs(raw_jitter) > .5) or np.any(projection[:2] <= 0):
        raise ValueError("invalid jitter or projection scale")
    if k < 0 or not 0 <= weight <= 1:
        raise ValueError("invalid TAA weighting metadata")
    jitter = (raw_jitter[0] * width / FULL_WIDTH,
              raw_jitter[1] * height / FULL_HEIGHT)
    return jitter, tuple(projection), k, weight


def load_existing_resolve(path: Path, width: int, height: int, crop: tuple[int, int, int, int]):
    """Compile the reviewed resolve function verbatim from taa_resolve_replay.py."""
    source = path.read_text()
    required = ("LINE_MARGIN = .1", "FAR_P22, FAR_P32 = 1.000003, -6.000018",
                "def resolve(cur, dep, mot, hist, pdep, age, j, k, w, Rc, Rp, P, conv, opt):",
                "def camera_previous_ndc(")
    if any(fragment not in source for fragment in required):
        raise ValueError("unsupported taa_resolve_replay.py contract")
    tree = ast.parse(source, filename=str(path))
    wanted = {"valid", "weigh", "unweigh", "camera_previous_ndc", "resolve"}
    functions = [node for node in tree.body if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
                 and node.name in wanted]
    if {node.name for node in functions} != wanted:
        raise ValueError("missing TAA replay function")
    x0, y0, x1, y1 = crop
    ys, xs = np.mgrid[y0:y1, x0:x1]
    namespace = {"np": np, "W": width, "H": height, "LUMA": LUMA,
                 "LINE_MARGIN": .1, "FAR_P22": 1.000003, "FAR_P32": -6.000018,
                 "X0": x0, "Y0": y0, "X1": x1, "Y1": y1, "ys": ys, "xs": xs}
    module = ast.fix_missing_locations(ast.Module(body=functions, type_ignores=[]))
    exec(compile(module, str(path), "exec"), namespace)
    resolve_source = ast.get_source_segment(source, next(node for node in functions if node.name == "resolve"))
    return namespace["resolve"], hashlib.sha256(resolve_source.encode()).hexdigest()


def bilinear(image: np.ndarray, x: np.ndarray, y: np.ndarray) -> np.ndarray:
    x0 = np.floor(x).astype(np.int64); y0 = np.floor(y).astype(np.int64)
    fx = (x - x0)[..., None]; fy = (y - y0)[..., None]
    x0 = np.clip(x0, 0, image.shape[1] - 1); x1 = np.clip(x0 + 1, 0, image.shape[1] - 1)
    y0 = np.clip(y0, 0, image.shape[0] - 1); y1 = np.clip(y0 + 1, 0, image.shape[0] - 1)
    return ((image[y0, x0] * (1 - fx) + image[y0, x1] * fx) * (1 - fy) +
            (image[y1, x0] * (1 - fx) + image[y1, x1] * fx) * fy)


def rotation_history_coordinates(width: int, height: int, crop: tuple[int, int, int, int],
                                 jitter: tuple[float, float], projection: tuple[float, float, float, float],
                                 current_rotation: np.ndarray, previous_rotation: np.ndarray):
    x0, y0, x1, y1 = crop
    ys, xs = np.mgrid[y0:y1, x0:x1]
    jx, jy = jitter; p00, p11, p20, p21 = projection
    nx, ny = 2 * (xs - jx) / width - 1, 1 - 2 * (ys - jy) / height
    view = np.stack([(nx - p20) / p00, (ny - p21) / p11, np.ones_like(nx)], -1)
    world = view @ current_rotation.T
    previous = world @ previous_rotation
    valid = previous[..., 2] > 1e-6
    pnx = previous[..., 0] / previous[..., 2] * p00 + p20
    pny = previous[..., 1] / previous[..., 2] * p11 + p21
    hx = (pnx * .5 + .5) * width + jx
    hy = (.5 - pny * .5) * height + jy
    valid &= (hx + .5 >= 0) & (hx + .5 <= width) & (hy + .5 >= 0) & (hy + .5 <= height)
    return hx, hy, valid


def display_codes(image: np.ndarray, k: float) -> np.ndarray:
    luminance = np.maximum(image[..., :3].astype(np.float64) @ LUMA, 0)
    return 255 * k * luminance / (1 + k * luminance)


def aggregate_hash(paths: list[Path]) -> str:
    joined = b"".join(bytes.fromhex(digest(path)) for path in paths)
    return hashlib.sha256(joined).hexdigest()


def validate_gpu_report(report: dict, report_path: Path, manifest_sha256: str,
                        frame_ids: list[int], composite_pattern: str) -> tuple[str, dict[str, str]]:
    """Validate either the retained prototype report or production runner binding."""
    if "parity" in report:
        if report.get("manifest_sha256") != manifest_sha256:
            raise ValueError("GPU report does not bind the sequence manifest")
        if not report.get("parity", {}).get("passed") or not report.get("composite", {}).get("passed"):
            raise ValueError("GPU sequence report did not pass")
        return "prototype", {}
    inputs_path = report_path.with_name("inputs.json")
    if not report.get("passed") or not report.get("numerical", {}).get("passed"):
        raise ValueError("production GPU sequence report did not pass")
    if not inputs_path.is_file() or digest(inputs_path) != report.get("inputs_sha256"):
        raise ValueError("production GPU report does not bind inputs.json")
    inputs = json.loads(inputs_path.read_text())
    if inputs.get("sequence_manifest", {}).get("sha256") != manifest_sha256:
        raise ValueError("production inputs do not bind the sequence manifest")
    cases = {(int(row["frame"]), int(row["variant"])): row
             for row in report["numerical"].get("cases", [])}
    if any(not cases.get((frame, 0), {}).get("passed") for frame in frame_ids):
        raise ValueError("production baseline numerical case missing or failed")
    hashes = {}
    for frame in frame_ids:
        name = composite_pattern.format(frame=frame)
        expected = report["numerical"].get("readback_hashes", {}).get(name)
        if isinstance(expected, dict):
            expected = expected.get("sha256")
        if not isinstance(expected, str) or len(expected) != 64:
            raise ValueError(f"production readback hash missing: {name}")
        hashes[name] = expected
    return "production", hashes


def output_contract(kind: str) -> tuple[str, list[str], str]:
    common = [
        "The sequence colour already contains native fog, so no clean no-fog baseline exists.",
        "Motion/depth are exact point reductions of full-resolution capture; this is not a native 120x72 TAA render.",
        "Resolved-versus-current deviation is a witness, not an image-quality pass threshold.",
        "No game, native-Windows, loading, GPU-upload or flight acceptance is established.",
    ]
    if kind == "prototype":
        return ("prototype GPU fixture composite; not actual production FogPass",
                [common[0],
                 "The 32 composites are retained prototype-fixture output, not actual production FogPass output.",
                 *common[1:]],
                "Run the actual production FogPass baseline composite for the same 32 case IDs and preserve one "
                "120x72 RGBA16F composite per frame; pass --composite-pattern "
                "'{frame}-v0.composite.rgba16f' to replace only the prototype composite source. "
                "A real in-game verdict still needs post-FogPass TAA/current captures during translated flight.")
    if kind == "production":
        return ("actual production FogPass baseline composite", common,
                "The production 32-frame substitution is complete. A real in-game verdict still needs "
                "post-FogPass TAA/current captures during translated flight.")
    raise ValueError("unknown GPU report kind")


def run(sequence: Path, readback: Path, capture: Path, metadata_path: Path,
        taa_source: Path, composite_pattern: str = "{frame}.composite.rgba16f") -> dict:
    manifest_path = sequence / "manifest.json"
    fixture_report_path = readback / "report.json"
    manifest = json.loads(manifest_path.read_text())
    fixture_report = json.loads(fixture_report_path.read_text())
    frame_ids = [frame for start, _ in BURSTS for frame in range(start, start + 8)]
    if (manifest.get("schema"), manifest.get("captured_frame_ids")) != (3, frame_ids):
        raise ValueError("expected frozen 32-frame sequence manifest")
    report_kind, expected_readbacks = validate_gpu_report(
        fixture_report, fixture_report_path, digest(manifest_path), frame_ids, composite_pattern)
    fog_output, limitations, next_step = output_contract(report_kind)
    metadata = json.loads(metadata_path.read_text())
    if set(map(str, frame_ids)) - set(metadata):
        raise ValueError("missing capture metadata")

    width, height = manifest["coordinate_contract"]["reduced_dimensions"]
    crop = (3, 3, width - 3, height - 3)
    resolve, resolve_sha = load_existing_resolve(taa_source, width, height, crop)
    ix, iy = centre_indices(FULL_WIDTH, width), centre_indices(FULL_HEIGHT, height)
    motion_paths, composite_paths = [], []
    bursts = []
    all_code_differences = []
    all_sky_code_differences = []
    all_fog_temporal_differences = []
    all_sky_fog_temporal_differences = []
    all_fog_mismatches = []

    for start, family in BURSTS:
        frames = list(range(start, start + 8))
        histories = []
        depths = []
        motions = []
        scenes = []
        rotations = []
        origins = []
        for frame in frames:
            case = sequence / str(frame)
            depth = np.fromfile(case / "depth.rgba32f", "<f4").reshape(height, width, 4)
            full_depth = np.memmap(capture / f"depth_1_{frame}.rgba32f", "<f4", mode="r",
                                   shape=(FULL_HEIGHT, FULL_WIDTH, 4))
            if not np.array_equal(depth, full_depth[np.ix_(iy, ix)]):
                raise ValueError(f"reduced depth provenance mismatch: {frame}")
            motion_path = capture / f"motion_1_{frame}.rgba32f"
            full_motion = np.memmap(motion_path, "<f4", mode="r",
                                    shape=(FULL_HEIGHT, FULL_WIDTH, 4))
            motion = np.asarray(full_motion[np.ix_(iy, ix)]).copy()
            scene = np.fromfile(case / "scene.rgba16f", "<f2").reshape(height, width, 4).astype(np.float32)
            composite_path = readback / composite_pattern.format(frame=frame)
            expected_readback = expected_readbacks.get(composite_path.name)
            if expected_readback is not None and digest(composite_path) != expected_readback:
                raise ValueError(f"production readback hash mismatch: {composite_path.name}")
            current = np.fromfile(composite_path, "<f2").reshape(height, width, 4).astype(np.float32)
            if not all(np.isfinite(a).all() for a in (depth, motion, scene, current)):
                raise ValueError(f"nonfinite frame input: {frame}")
            rotation, origin = camera(metadata[str(frame)])
            histories.append(current); depths.append(depth[..., 0].copy()); motions.append(motion)
            scenes.append(scene); rotations.append(rotation); origins.append(origin)
            motion_paths.append(motion_path); composite_paths.append(composite_path)

        history = histories[0].copy()
        scene_history = scenes[0].copy()
        previous_depth = depths[0].copy()
        age = np.ones((crop[3] - crop[1], crop[2] - crop[0]), np.float64)
        rows = []
        for index in range(1, len(frames)):
            frame = frames[index]; meta = metadata[str(frame)]
            motion_row = meta["motion_output_frame"]; camera_row = meta["camera_state"]
            jitter, projection, k, weight = temporal_parameters(meta, width, height)
            resolved, next_age, diagnostic = resolve(
                histories[index], depths[index], motions[index], history, previous_depth, age,
                jitter, k, weight, rotations[index], rotations[index - 1], projection, 1, {})
            resolved_scene, _, _ = resolve(
                scenes[index], depths[index], motions[index], scene_history, previous_depth, age,
                jitter, k, weight, rotations[index], rotations[index - 1], projection, 1, {})
            age = next_age
            if not np.isfinite(resolved).all() or not np.isfinite(resolved_scene).all():
                raise ValueError(f"nonfinite TAA result: {frame}")
            x0, y0, x1, y1 = crop
            current_crop = histories[index][y0:y1, x0:x1]
            code_difference = np.abs(display_codes(resolved, k) - display_codes(current_crop, k))
            current_fog_codes = display_codes(histories[index], k)[y0:y1, x0:x1] - display_codes(
                scenes[index], k)[y0:y1, x0:x1]
            resolved_fog_codes = display_codes(resolved, k) - display_codes(resolved_scene, k)
            fog_temporal_difference = np.abs(resolved_fog_codes - current_fog_codes)
            finite = np.isfinite(resolved).all(axis=-1)
            sky = diagnostic["far"] & ~diagnostic["thin"] & diagnostic["accept"]
            geometry = ~diagnostic["far"]
            hx, hy, history_valid = rotation_history_coordinates(
                width, height, crop, jitter, projection, rotations[index], rotations[index - 1])
            current_effect = histories[index] - scenes[index]
            previous_effect = histories[index - 1] - scenes[index - 1]
            sampled_effect = bilinear(previous_effect[..., :3], hx, hy)
            fog_mismatch = np.max(np.abs(current_effect[y0:y1, x0:x1, :3] - sampled_effect), axis=-1)
            witness_mask = sky & history_valid
            translation = float(np.linalg.norm(origins[index] - origins[index - 1]))
            row = {
                "frame": frame,
                "camera_translation_units": translation,
                "camera_rotation_deg": float(motion_row["camera_rotation_deg"]),
                "finite_resolved_pixels": int(finite.sum()),
                "crop_pixels": int(finite.size),
                "accepted_history_pixels": int(diagnostic["accept"].sum()),
                "thin_foreground_pixels": int(diagnostic["thin"].sum()),
                "stable_sky_resolved_vs_current_luma_codes": metrics(code_difference[sky]),
                "temporal_change_to_incremental_fog_luma_codes": {
                    "all": metrics(fog_temporal_difference),
                    "stable_sky": metrics(fog_temporal_difference[sky]),
                    "geometry": metrics(fog_temporal_difference[geometry]),
                    "thin_foreground": metrics(fog_temporal_difference[diagnostic["thin"]]),
                },
                "rotation_only_sky_fog_effect_rgb_abs": metrics(fog_mismatch[witness_mask]),
            }
            rows.append(row)
            all_code_differences.append(code_difference)
            all_sky_code_differences.append(code_difference[sky])
            all_fog_temporal_differences.append(fog_temporal_difference)
            all_sky_fog_temporal_differences.append(fog_temporal_difference[sky])
            all_fog_mismatches.append(fog_mismatch[witness_mask])
            history = histories[index].copy()
            history[y0:y1, x0:x1] = resolved.astype(np.float32)
            scene_history = scenes[index].copy()
            scene_history[y0:y1, x0:x1] = resolved_scene.astype(np.float32)
            previous_depth = depths[index]
        bursts.append({
            "start": start, "family": family, "frames": frames,
            "history_seed": "frame 0 current fog composite after one-time invalidation",
            "translation_per_step_units": metrics(np.array([r["camera_translation_units"] for r in rows])),
            "rotation_per_step_degrees": metrics(np.array([r["camera_rotation_deg"] for r in rows])),
            "results": rows,
        })

    source_contract = {
        "sequence_manifest_sha256": digest(manifest_path),
        "metadata_sha256": digest(metadata_path),
        "taa_replay_sha256": digest(taa_source),
        "taa_resolve_function_sha256": resolve_sha,
        "motion_files_aggregate_sha256": aggregate_hash(motion_paths),
    }
    if report_kind == "prototype":
        source_contract.update(
            prototype_gpu_report_sha256=digest(fixture_report_path),
            prototype_gpu_executable_sha256=fixture_report.get("executable_sha256"),
            prototype_composites_aggregate_sha256=aggregate_hash(composite_paths))
    else:
        source_contract.update(
            production_gpu_report_sha256=digest(fixture_report_path),
            production_inputs_sha256=fixture_report.get("inputs_sha256"),
            production_gpu_executable_sha256=fixture_report.get("execution", {}).get("executable_sha256"),
            production_composites_aggregate_sha256=aggregate_hash(composite_paths))
    return {
        "schema": 1,
        "result": "bounded-derived-witness-not-acceptance",
        "dimensions": [width, height], "crop": list(crop),
        "sources": source_contract,
        "input_facts": {
            "captured_frames": len(frame_ids),
            "translation_is_captured_not_synthetic": True,
            "motion_is_exact_point_reduction_of_run185": True,
            "jitter_scale": [width / FULL_WIDTH, height / FULL_HEIGHT],
            "history_route": "existing sky rotation-only plus captured routed surface motion",
            "fog_output": fog_output,
            "composite_pattern": composite_pattern,
        },
        "summary": {
            "all_resolved_vs_current_luma_codes": metrics(np.concatenate([a.ravel() for a in all_code_differences])),
            "stable_sky_resolved_vs_current_luma_codes": metrics(np.concatenate(all_sky_code_differences)),
            "temporal_change_to_incremental_fog_luma_codes": metrics(
                np.concatenate([a.ravel() for a in all_fog_temporal_differences])),
            "stable_sky_temporal_change_to_incremental_fog_luma_codes": metrics(
                np.concatenate(all_sky_fog_temporal_differences)),
            "rotation_only_sky_fog_effect_rgb_abs": metrics(np.concatenate(all_fog_mismatches)),
            "all_outputs_finite": all(r["finite_resolved_pixels"] == r["crop_pixels"]
                                      for burst in bursts for r in burst["results"]),
        },
        "bursts": bursts,
        "limits": limitations,
        "next_step": next_step,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sequence", type=Path, required=True)
    parser.add_argument("--readback", type=Path, required=True)
    parser.add_argument("--capture", type=Path, required=True)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--taa-replay", type=Path,
                        default=Path(__file__).with_name("taa_resolve_replay.py"))
    parser.add_argument("--composite-pattern", default="{frame}.composite.rgba16f")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = run(args.sequence, args.readback, args.capture, args.metadata, args.taa_replay,
                 args.composite_pattern)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, sort_keys=True, allow_nan=False,
                                      separators=(",", ":")) + "\n")
    print(json.dumps({"output": str(args.output), "summary": result["summary"]}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
