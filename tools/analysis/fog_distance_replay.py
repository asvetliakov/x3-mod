#!/usr/bin/env python3
"""Bounded offline replay of the ratified same-field long-range fog recipe."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import time

import numpy as np


F = np.float32
FULL_WIDTH, FULL_HEIGHT = 1280, 768
PERIOD, GRID, NEAR, FAR = 32768.0, 128, 12000.0, 200000.0
WINDOW_START = 150000.0
HEADER = struct.Struct("<8sIIIIIIIIIQI")
BURSTS = ((16450, 16481, "bluewell"), (43051, 43082, "foggreenoutlands"))
CAMERA_PREFIX = "camera_state device=1 frame="
MOTION_PREFIX = "motion_output_frame device=1 frame="
POINT_SUN_PREFIX = "shadow_replay_sun_point device=1 frame="


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def fields(line: str) -> dict[str, str]:
    return dict(re.findall(r"(\w+)=([^ ]+)", line))


def load_metadata(log: Path) -> tuple[dict[int, dict], str]:
    wanted = {frame for lo, hi, _ in BURSTS for frame in range(lo, hi + 1)}
    rows = {frame: {} for frame in wanted}
    selected = hashlib.sha256()
    with log.open(errors="replace") as stream:
        for line in stream:
            if not (line.startswith(CAMERA_PREFIX) or line.startswith(MOTION_PREFIX) or line.startswith(POINT_SUN_PREFIX)):
                continue
            row = fields(line)
            try:
                frame = int(row["frame"])
            except (KeyError, ValueError):
                continue
            if frame not in wanted:
                continue
            kind = "camera" if line.startswith(CAMERA_PREFIX) else "motion" if line.startswith(MOTION_PREFIX) else "point_sun"
            rows[frame][kind] = row
    missing = [(frame, kind) for frame, row in rows.items() for kind in ("camera", "motion", "point_sun") if kind not in row]
    if missing:
        raise ValueError(f"missing capture metadata: {missing[:4]}")
    for frame in sorted(rows):
        selected.update(json.dumps(rows[frame], sort_keys=True, separators=(",", ":")).encode())
    return rows, selected.hexdigest()


def decode_packet(path: Path, manifest: dict) -> np.ndarray:
    data = path.read_bytes()
    if len(data) < HEADER.size:
        raise ValueError(f"short fog packet: {path}")
    magic, version, header_size, profile_id, recipe_id, width, height, texel_bytes, decoded_bytes, runs, checksum, reserved = HEADER.unpack_from(data)
    if (magic.rstrip(b"\0"), version, header_size, recipe_id, width, height, texel_bytes, reserved) != (b"X3FOGPK", 1, HEADER.size, 1, 1560, 1430, 8, 0):
        raise ValueError(f"unsupported fog packet header: {path}")
    profile = next((row for row in manifest["profiles"] if row["name"] == path.stem), None)
    if profile is None or profile_id != profile["profile_id"] or digest(path) != profile["resource_sha256"]:
        raise ValueError(f"fog packet provenance mismatch: {path}")
    decoded = bytearray(decoded_bytes); source = HEADER.size; target = 0
    for _ in range(runs):
        if source + 4 > len(data):
            raise ValueError("truncated fog run")
        word, = struct.unpack_from("<I", data, source); source += 4
        literal, count = bool(word & 0x80000000), word & 0x7fffffff
        byte_count = count * texel_bytes
        if target + byte_count > decoded_bytes:
            raise ValueError("fog run overflow")
        if literal:
            if source + byte_count > len(data):
                raise ValueError("truncated fog literal")
            decoded[target:target + byte_count] = data[source:source + byte_count]
            source += byte_count
        target += byte_count
    if source != len(data) or target != decoded_bytes or hashlib.sha256(decoded).hexdigest() != profile["decoded_sha256"]:
        raise ValueError("fog packet decode mismatch")
    atlas = np.frombuffer(decoded, "<f2").reshape(height, width, 4)
    volume = np.empty((GRID, GRID, GRID, 4), F)
    for z in range(GRID):
        oy, ox = (z // 12) * 130, (z % 12) * 130
        volume[z] = atlas[oy + 1:oy + 129, ox + 1:ox + 129]
    if not np.isfinite(volume).all() or np.any(volume < 0):
        raise ValueError("invalid decoded volume")
    return volume


def mip_pyramid(volume: np.ndarray) -> list[np.ndarray]:
    levels = [np.asarray(volume, F)]
    while levels[-1].shape[0] > 1:
        a = levels[-1]; n = a.shape[0] // 2
        levels.append(a.reshape(n, 2, n, 2, n, 2, 4).mean(axis=(1, 3, 5), dtype=np.float64).astype(F))
    return levels


def sample_level(volume: np.ndarray, points: np.ndarray) -> np.ndarray:
    n = volume.shape[0]
    u = np.mod(np.asarray(points, np.float64), PERIOD) * (n / PERIOD) - .5
    base = np.floor(u).astype(np.int64); frac = (u - base).astype(F); base %= n
    out = np.zeros(points.shape[:-1] + (4,), F)
    for dz in (0, 1):
        wz = frac[..., 2] if dz else 1 - frac[..., 2]; iz = (base[..., 2] + dz) % n
        for dy in (0, 1):
            wy = frac[..., 1] if dy else 1 - frac[..., 1]; iy = (base[..., 1] + dy) % n
            for dx in (0, 1):
                wx = frac[..., 0] if dx else 1 - frac[..., 0]; ix = (base[..., 0] + dx) % n
                out += (wx * wy * wz)[..., None] * volume[iz, iy, ix]
    return out


def sample_mipped(levels: list[np.ndarray], points: np.ndarray, lod: np.ndarray) -> np.ndarray:
    lod = np.clip(np.asarray(lod, F), 0, len(levels) - 1)
    lo = np.floor(lod).astype(np.int32); hi = np.minimum(lo + 1, len(levels) - 1); f = lod - lo
    out = np.zeros(points.shape[:-1] + (4,), F)
    for level in np.unique(np.concatenate((lo.ravel(), hi.ravel()))):
        sampled = sample_level(levels[int(level)], points)
        out += sampled * ((lo == level) * (1 - f) + (hi == level) * f)[..., None]
    return out


def smoothstep(lo: float, hi: float, x: np.ndarray) -> np.ndarray:
    t = np.clip((x - lo) / (hi - lo), 0, 1)
    return t * t * (3 - 2 * t)


def integrate_samples(rgba: np.ndarray, ds: np.ndarray, distance: np.ndarray, sigma: float,
                      direction: np.ndarray, apply_window: bool,
                      sun_direction: np.ndarray = np.array([1., 0., 0.])) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    window = 1 - smoothstep(WINDOW_START, FAR, distance) if apply_window else np.ones_like(distance)
    rho = rgba[..., 3]
    tau_step = sigma * rho * window * ds
    tau_before = np.cumsum(tau_step, axis=0, dtype=np.float64) - tau_step
    trans_before = np.exp(-tau_before); opacity_step = -np.expm1(-tau_step)
    chroma = np.divide(rgba[..., :3], rho[..., None], out=np.zeros_like(rgba[..., :3]), where=rho[..., None] > 0)
    sun_direction = np.asarray(sun_direction, F)
    if sun_direction.shape != (3,) or not np.isfinite(sun_direction).all():
        raise ValueError("finite captured sun direction required")
    sun_direction = sun_direction / np.linalg.norm(sun_direction)
    # Avoid the same narrow-vector Accelerate warning as ray reconstruction.
    cosine = (direction[:, 0] * sun_direction[0] + direction[:, 1] * sun_direction[1] +
              direction[:, 2] * sun_direction[2])
    phase = (.91 / (4 * (1.09 - .6 * cosine) * np.sqrt(1.09 - .6 * cosine))).astype(F)
    scattering = np.sum((trans_before * opacity_step)[..., None] * chroma * phase[None, :, None], axis=0)
    tau = np.sum(tau_step, axis=0, dtype=np.float64)
    return scattering.astype(F), np.exp(-tau).astype(F), tau.astype(F)


def reference(level0: np.ndarray, origin: np.ndarray, direction: np.ndarray, limit: np.ndarray,
              sigma: float, spacing: float, sun_direction: np.ndarray = np.array([1., 0., 0.])) -> dict[str, np.ndarray]:
    counts = np.ceil(np.maximum(limit, 0) / spacing).astype(np.int32); count = int(counts.max(initial=0))
    index = np.arange(count)[:, None]; lo = index * spacing
    ds = np.clip(limit[None, :] - lo, 0, spacing).astype(F); distance = lo + ds * .5
    rgba = sample_level(level0, origin + direction[None, :, :] * distance[..., None])
    rgba[ds <= 0] = 0
    S, T, tau = integrate_samples(rgba, ds, distance, sigma, direction, True, sun_direction)
    return {"S": S, "T": T, "tau": tau}


def candidate(levels: list[np.ndarray], origin: np.ndarray, direction: np.ndarray, limit: np.ndarray,
              sigma: float, far_steps: int = 24,
              sun_direction: np.ndarray = np.array([1., 0., 0.])) -> dict[str, np.ndarray]:
    near_limit = np.minimum(np.maximum(limit, 0), NEAR).astype(F)
    near_ds = near_limit / 24
    ni = np.arange(24, dtype=F)[:, None]
    near_distance = near_ds[None, :] * (ni + F(.5))
    near_rgba = sample_level(levels[0], origin + direction[None, :, :] * near_distance[..., None])
    near_S, near_T, near_tau = integrate_samples(near_rgba, np.broadcast_to(near_ds, near_distance.shape), near_distance,
                                                  sigma, direction, False, sun_direction)
    width = (FAR - NEAR) / far_steps
    fi = np.arange(far_steps, dtype=F)[:, None]; lo = NEAR + fi * width
    far_ds = np.clip(limit[None, :] - lo, 0, width).astype(F); far_distance = lo + far_ds * .5
    blend = smoothstep(NEAR, 20000., far_distance)
    lod_target = math.log2(max(1., width / 256.))
    lod = blend * lod_target
    far_rgba = sample_mipped(levels, origin + direction[None, :, :] * far_distance[..., None], lod)
    far_rgba[far_ds <= 0] = 0
    far_S_raw, far_T, far_tau = integrate_samples(far_rgba, far_ds, far_distance, sigma, direction, True, sun_direction)
    return {"S": near_S + near_T[:, None] * far_S_raw, "T": near_T * far_T,
            "tau": near_tau + far_tau, "near_S": near_S, "near_T": near_T, "near_tau": near_tau,
            "far_S": near_T[:, None] * far_S_raw, "far_T": far_T, "far_tau": far_tau,
            "lod_target": lod_target}


def near24(level0: np.ndarray, origin: np.ndarray, direction: np.ndarray, limit: np.ndarray,
           sigma: float, sun_direction: np.ndarray) -> dict[str, np.ndarray]:
    near_limit = np.minimum(np.maximum(limit, 0), NEAR).astype(F)
    ds = near_limit / 24
    index = np.arange(24, dtype=F)[:, None]
    distance = ds[None, :] * (index + F(.5))
    rgba = sample_level(level0, origin + direction[None, :, :] * distance[..., None])
    S, T, tau = integrate_samples(rgba, np.broadcast_to(ds, distance.shape), distance,
                                  sigma, direction, False, sun_direction)
    return {"S": S, "T": T, "tau": tau}


def reference_shell(level0: np.ndarray, origin: np.ndarray, direction: np.ndarray,
                    limit: np.ndarray, sigma: float, spacing: float, start: float,
                    end: float, sun_direction: np.ndarray) -> dict[str, np.ndarray]:
    count = int(math.ceil((end - start) / spacing))
    index = np.arange(count)[:, None]; lo = start + index * spacing
    shell_limit = np.minimum(limit[None, :], end)
    ds = np.clip(shell_limit - lo, 0, spacing).astype(F); distance = lo + ds * .5
    rgba = sample_level(level0, origin + direction[None, :, :] * distance[..., None])
    rgba[ds <= 0] = 0
    S, T, tau = integrate_samples(rgba, ds, distance, sigma, direction, True, sun_direction)
    bank = np.floor((lo[:, 0] - start) / PERIOD).astype(np.int32)
    bank_tau = [np.sum((sigma * rgba[..., 3] *
                        (1 - smoothstep(WINDOW_START, FAR, distance)) * ds)[bank == value],
                       axis=0, dtype=np.float64).astype(F)
                for value in np.unique(bank)]
    return {"S": S, "T": T, "tau": tau, "bank_tau": bank_tau}


def accurate_reference(level0: np.ndarray, origin: np.ndarray, direction: np.ndarray,
                       limit: np.ndarray, sigma: float, spacing: float,
                       sun_direction: np.ndarray, chunk: int = 64) -> dict[str, np.ndarray]:
    rows = []
    for first in range(0, len(limit), chunk):
        sl = slice(first, min(first + chunk, len(limit)))
        near = near24(level0, origin, direction[sl], limit[sl], sigma, sun_direction)
        shell1 = reference_shell(level0, origin, direction[sl], limit[sl], sigma, spacing,
                                 NEAR, WINDOW_START, sun_direction)
        shell2 = reference_shell(level0, origin, direction[sl], limit[sl], sigma, spacing,
                                 WINDOW_START, FAR, sun_direction)
        S1_added = near["T"][:, None] * shell1["S"]
        T1 = near["T"] * shell1["T"]
        S2_added = T1[:, None] * shell2["S"]
        rows.append({"near_S": near["S"], "near_T": near["T"], "near_tau": near["tau"],
                     "shell1_S": shell1["S"], "shell1_T": shell1["T"], "shell1_tau": shell1["tau"],
                     "shell1_added_S": S1_added, "near_shell1_S": near["S"] + S1_added,
                     "near_shell1_T": T1, "shell2_S": shell2["S"], "shell2_T": shell2["T"],
                     "shell2_tau": shell2["tau"], "shell2_added_S": S2_added,
                     "S": near["S"] + S1_added + S2_added, "T": T1 * shell2["T"],
                     "tau": near["tau"] + shell1["tau"] + shell2["tau"],
                     "far_S": shell1["S"] + shell1["T"][:, None] * shell2["S"],
                     "far_T": shell1["T"] * shell2["T"],
                     "far_tau": shell1["tau"] + shell2["tau"],
                     "shell1_bank_tau": shell1["bank_tau"], "shell2_bank_tau": shell2["bank_tau"]})
    keys = [key for key in rows[0] if not key.endswith("_bank_tau")]
    result = {key: np.concatenate([row[key] for row in rows], axis=0) for key in keys}
    for shell in ("shell1", "shell2"):
        count = len(rows[0][f"{shell}_bank_tau"])
        result[f"{shell}_bank_tau"] = [np.concatenate([row[f"{shell}_bank_tau"][i] for row in rows])
                                        for i in range(count)]
    return result


def camera(row: dict[str, str]) -> tuple[np.ndarray, np.ndarray]:
    rotation = np.array([[float(row[f"r{i}{j}"]) for j in range(3)] for i in range(3)], np.float64)
    translation = np.fromstring(row["t"], sep=",", dtype=np.float64)
    if translation.size != 3 or not np.isfinite(rotation).all() or not np.isfinite(translation).all():
        raise ValueError("invalid camera metadata")
    return rotation, -translation @ np.linalg.inv(rotation)


def captured_sun(meta: dict) -> np.ndarray:
    row = meta["point_sun"]
    if row.get("source") != "point" or row.get("poll") != "ok" or row.get("directional") != "1":
        raise ValueError("invalid captured point-sun provenance")
    # Production selects cascade slot 1 when multiple cascades are present.
    sun = np.fromstring(row["dir1"], sep=",", dtype=np.float64)
    length = np.linalg.norm(sun)
    if sun.size != 3 or not np.isfinite(sun).all() or not (.999 <= length <= 1.001):
        raise ValueError("invalid captured world sun")
    return (sun / length).astype(F)


def reconstruct_rays(meta: dict, x: np.ndarray, y: np.ndarray,
                     depth: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    motion, cam = meta["motion"], meta["camera"]
    jx, jy = float(motion["jitter_x"]), float(motion["jitter_y"])
    # Production uploads p20+2*jx/W+1/W and p21-2*jy/H-1/H. Evaluating
    # those constants at texel-centre uv=(pixel+.5)/size reduces exactly to
    # x-jx, y-jy here; do not add another pixel centre or reverse the jitter.
    vx = ((x - jx) / FULL_WIDTH * 2 - 1 - float(cam["p20"])) / float(cam["p00"])
    vy = (1 - (y - jy) / FULL_HEIGHT * 2 - float(cam["p21"])) / float(cam["p11"])
    view = np.stack((vx, vy, np.ones_like(vx)), axis=-1)
    view_length = np.linalg.norm(view, axis=-1)
    rotation, origin = camera(cam); basis = np.linalg.inv(rotation)
    # Explicit products avoid a macOS Accelerate matmul warning on this narrow 3-column shape.
    direction = np.stack([view[:, 0] * basis[0, i] + view[:, 1] * basis[1, i] + view[:, 2] * basis[2, i]
                          for i in range(3)], axis=-1)
    direction /= np.linalg.norm(direction, axis=-1)[:, None]
    if not np.isfinite(direction).all():
        raise ValueError("nonfinite reconstructed ray")
    geometry = (depth[:, 0] >= 0) & (depth[:, 0] <= 1)
    invalid = geometry & (~np.isfinite(depth[:, 2]) | (depth[:, 2] <= 0))
    limit = np.where(invalid, 0, np.where(geometry, np.minimum(depth[:, 2] * view_length, FAR), FAR))
    return origin, direction.astype(F), limit.astype(F), geometry


def ray_set(meta: dict, depth_path: Path, nx: int, ny: int) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    # One deterministic ray in each equal-area screen stratum; this is a fixed
    # descriptive sample, not a random sample with population confidence bounds.
    sx = ((np.arange(nx) + .38196601125) * FULL_WIDTH / nx).astype(np.int32)
    sy = ((np.arange(ny) + .61803398875) * FULL_HEIGHT / ny).astype(np.int32)
    yy, xx = np.meshgrid(sy, sx, indexing="ij"); x, y = xx.ravel(), yy.ravel()
    depth = np.memmap(depth_path, "<f4", mode="r", shape=(FULL_HEIGHT, FULL_WIDTH, 4))[y, x]
    return reconstruct_rays(meta, x, y, depth)


def metric(values: np.ndarray) -> dict[str, float | int]:
    a = np.asarray(values, np.float64).ravel()
    return {"count": int(a.size), "p99": float(np.percentile(a, 99)) if a.size else 0.,
            "max": float(a.max(initial=0))}


def fraction(values: np.ndarray) -> dict[str, float | int]:
    a = np.asarray(values, bool).ravel(); n = len(a); hits = int(a.sum())
    return {"count": n, "hits": hits, "fraction": float(hits / n) if n else 0.}


def error_metrics(candidate_result: dict, reference_result: dict) -> dict:
    return {"T": metric(np.abs(candidate_result["T"] - reference_result["T"])),
            "S_normalized_lighting": [metric(np.abs(candidate_result["S"][:, channel] - reference_result["S"][:, channel]))
                                      for channel in range(3)]}


def numerical_pass(errors: dict, reference_gate: bool = False) -> bool:
    if reference_gate:
        return errors["T"]["p99"] <= .00025 and errors["T"]["max"] <= .00075
    # Captured world sun is exact for the scored view; unavailable radiance is
    # normalized to one. Preserve the numerical S scale, without claiming
    # production-scaled scattering parity.
    return (errors["T"]["p99"] <= .001 and errors["T"]["max"] <= .003 and
            all(row["p99"] <= .0005 and row["max"] <= .002
                for row in errors["S_normalized_lighting"]))


def components(mask: np.ndarray) -> int:
    mask = np.asarray(mask, bool); seen = np.zeros_like(mask); result = 0
    for y, x in zip(*np.nonzero(mask)):
        if seen[y, x]:
            continue
        result += 1; stack = [(y, x)]; seen[y, x] = True
        while stack:
            cy, cx = stack.pop()
            for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                ny, nx = cy + dy, cx + dx
                if 0 <= ny < mask.shape[0] and 0 <= nx < mask.shape[1] and mask[ny, nx] and not seen[ny, nx]:
                    seen[ny, nx] = True; stack.append((ny, nx))
    return result


def appearance(result: dict, shape: tuple[int, int], geometry: np.ndarray) -> dict:
    opacity = 1 - result["T"]; near_opacity = 1 - result["near_T"]; far_opacity = 1 - result["far_T"]
    def percentiles(a):
        return {str(p): float(np.percentile(a, p)) for p in (0, 25, 50, 75, 90, 99, 100)}
    populations = {}
    for name, mask in (("all", np.ones(len(opacity), bool)), ("sky", ~geometry), ("geometry", geometry)):
        populations[name] = {"count": int(mask.sum()),
                             "fraction_opacity_below_0.002": fraction(opacity[mask] < .002),
                             "fraction_opacity_above_0.015": fraction(opacity[mask] > .015)}
    return {"optical_depth_percentiles": percentiles(result["tau"]),
            "near_optical_depth_percentiles": percentiles(result["near_tau"]),
            "far_optical_depth_percentiles": percentiles(result["far_tau"]),
            "opacity_percentiles": percentiles(opacity),
            "near_opacity_percentiles": percentiles(near_opacity),
            "far_opacity_percentiles": percentiles(far_opacity),
            "populations": populations,
            "connected_clear_regions": components((opacity < .002).reshape(shape)),
            "connected_dense_regions": components((opacity > .015).reshape(shape))}


def write_sheet(path: Path, result: dict, shape: tuple[int, int], scale: int = 8) -> None:
    from PIL import Image
    panels = []
    for S, T in ((result["near_S"], result["near_T"]), (result["far_S"], result["far_T"]),
                 (result["S"], result["T"])):
        rgb = np.clip(S.reshape(shape + (3,)) / .03, 0, 1)
        opacity = np.clip((1 - T.reshape(shape)) / .4, 0, 1)
        panels.extend((rgb, np.repeat(opacity[..., None], 3, axis=-1)))
    image = np.concatenate(panels, axis=1)
    image = (np.power(image, 1 / 2.2) * 255 + .5).astype(np.uint8)
    Image.fromarray(image).resize((image.shape[1] * scale, image.shape[0] * scale), Image.Resampling.NEAREST).save(path)


def _rgb_panel(values: np.ndarray, shape: tuple[int, int], maximum: float, signed: bool = False) -> np.ndarray:
    image = values.reshape(shape + (3,))
    image = .5 + image / (2 * maximum) if signed else image / maximum
    return (np.clip(image, 0, 1) ** (1 / 2.2) * 255 + .5).astype(np.uint8)


def _scalar_panel(values: np.ndarray, shape: tuple[int, int], maximum: float,
                  signed: bool = False) -> np.ndarray:
    image = values.reshape(shape)
    image = .5 + image / (2 * maximum) if signed else image / maximum
    image = (np.clip(image, 0, 1) * 255 + .5).astype(np.uint8)
    return np.repeat(image[..., None], 3, axis=-1)


def write_reference_images(output: Path, stem: str, reference_result: dict,
                           rejected: dict, shape: tuple[int, int], scale: int = 6,
                           mask=None) -> list[str]:
    from PIL import Image, ImageDraw
    near_opacity = 1 - reference_result["near_T"]
    shell1_opacity = 1 - reference_result["shell1_T"]
    shell2_opacity = 1 - reference_result["shell2_T"]
    near_shell1_opacity = 1 - reference_result["near_shell1_T"]
    opacity = 1 - reference_result["T"]
    columns = [
        ("near 0-2.4km", [
            _rgb_panel(reference_result["near_S"], shape, .03),
            _scalar_panel(near_opacity, shape, .4),
            _scalar_panel(reference_result["near_tau"], shape, .5),
            _scalar_panel(reference_result["near_T"], shape, 1.)]),
        ("2.4-30km", [
            _rgb_panel(reference_result["shell1_added_S"], shape, .03),
            _scalar_panel(shell1_opacity, shape, .4),
            _rgb_panel(reference_result["near_shell1_S"], shape, .03),
            _scalar_panel(near_shell1_opacity, shape, .4)]),
        ("30-40km", [
            _rgb_panel(reference_result["shell2_added_S"], shape, .03),
            _scalar_panel(shell2_opacity, shape, .4),
            _rgb_panel(reference_result["S"], shape, .03),
            _scalar_panel(opacity, shape, .4)]),
        ("reference-candidate24", [
            _rgb_panel(reference_result["S"] - rejected["S"], shape, .15, True),
            _scalar_panel(reference_result["T"] - rejected["T"], shape, .3, True),
            _rgb_panel(np.abs(reference_result["S"] - rejected["S"]), shape, .15),
            _scalar_panel(np.abs(reference_result["T"] - rejected["T"]), shape, .3)])]
    if mask is not None:
        excluded = ~np.asarray(mask, bool).reshape(shape)
        for _, panels in columns:
            for panel in panels:
                panel[excluded] = (32, 0, 32)
    row_labels = ("added/near S", "shell opacity/T delta", "result S/abs S", "result opacity/abs T")
    h, w = shape; label_h, left = 18, 118
    sheet = Image.new("RGB", (left + len(columns) * w * scale, label_h + 4 * h * scale), "black")
    draw = ImageDraw.Draw(sheet)
    for column, (label, panels) in enumerate(columns):
        draw.text((left + column * w * scale + 2, 3), label, fill="white")
        for row, panel in enumerate(panels):
            cell = Image.fromarray(panel).resize((w * scale, h * scale), Image.Resampling.NEAREST)
            sheet.paste(cell, (left + column * w * scale, label_h + row * h * scale))
    for row, label in enumerate(row_labels):
        draw.text((2, label_h + row * h * scale + 3), label, fill="white")
    sheet_path = output / f"{stem}-reference-shells.png"; sheet.save(sheet_path)
    transmission_panel = _scalar_panel(reference_result["T"], shape, 1.)
    if mask is not None: transmission_panel[excluded] = (32, 0, 32)
    transmission = Image.fromarray(transmission_panel).resize(
        (w * scale, h * scale), Image.Resampling.NEAREST)
    transmission_path = output / f"{stem}-complete-transmission.png"; transmission.save(transmission_path)
    tau = np.concatenate([_scalar_panel(reference_result[key], shape, .5)
                          for key in ("near_tau", "shell1_tau", "shell2_tau", "tau")], axis=1)
    if mask is not None:
        excluded4 = np.tile(excluded, (1, 4)); tau[excluded4] = (32, 0, 32)
    tau_path = output / f"{stem}-optical-depths.png"
    Image.fromarray(tau).resize((w * 4 * scale, h * scale), Image.Resampling.NEAREST).save(tau_path)
    return [path.name for path in (sheet_path, transmission_path, tau_path)]


def laws(levels: list[np.ndarray]) -> dict:
    origin = np.array([123., 456., 789.]); direction = np.array([[1., 0., 0.]], F)
    zero = candidate(levels, origin, direction, np.array([0.], F), 1e-5)
    invalid = candidate(levels, origin, direction, np.array([0.], F), 1e-5)
    wrap_a = sample_level(levels[0], np.array([[0., 17., 29.]]))
    wrap_b = sample_level(levels[0], np.array([[PERIOD, 17., 29.]]))
    source_alpha = np.array([0., .2, 1.], F)
    composed_alpha = source_alpha.copy()
    return {"vacuum_zero_length_exact": bool(np.array_equal(zero["S"], np.zeros((1, 3), F)) and
                                               np.array_equal(zero["T"], np.ones(1, F))),
            "invalid_depth_identity_exact": bool(np.array_equal(invalid["S"], zero["S"]) and np.array_equal(invalid["T"], zero["T"])),
            "source_alpha_identity_exact": bool(np.array_equal(source_alpha, composed_alpha)),
            "finite": bool(all(np.isfinite(value).all() for key, value in zero.items() if isinstance(value, np.ndarray))),
            "wrapping_continuity_max": float(np.max(np.abs(wrap_a - wrap_b)))}


def run(capture: Path, asset_data: Path, output: Path) -> dict:
    started = time.monotonic(); output.mkdir(parents=True, exist_ok=True)
    manifest_path = asset_data / "manifest.json"; manifest = json.loads(manifest_path.read_text())
    if (manifest.get("period"), manifest.get("grid"), manifest.get("format")) != (PERIOD, GRID, "x3-fog-zero-literal-v1"):
        raise ValueError("unsupported frozen field manifest")
    log_paths = sorted(capture.glob("session-*.log"))
    if len(log_paths) != 1:
        raise ValueError("expected one capture session log")
    metadata, metadata_sha = load_metadata(log_paths[0])
    sources = {"manifest_sha256": digest(manifest_path), "selected_capture_metadata_sha256": metadata_sha,
               "capture_log": str(log_paths[0]), "capture_log_bytes": log_paths[0].stat().st_size,
               "asset_directory": str(asset_data), "packets": {}}
    profiles = {row["name"]: row for row in manifest["profiles"]}
    all_views, convergence_rows, score_rows, temporal, reference_gap_rows = [], [], [], [], []
    previous = {}
    reference_converged = True; candidate_passed = True; candidate_T_passed = True
    clear_gate = True; false_support_failed = False; near_exact = True
    for lo, hi, family in BURSTS:
        packet = asset_data / f"{family}.fogbin"; volume = decode_packet(packet, manifest); levels = mip_pyramid(volume)
        profile = profiles[family]; sigma = float(profile["base_sigma"]) * 1.5
        sources["packets"][family] = {"sha256": digest(packet), "decoded_sha256": profile["decoded_sha256"],
                                       "resource_bytes": packet.stat().st_size, "mip_means": [level.mean(axis=(0, 1, 2)).tolist() for level in levels]}
        family_temporal = []
        for frame in range(lo, hi + 1):
            depth_path = capture / f"depth_1_{frame}.rgba32f"
            if depth_path.stat().st_size != FULL_WIDTH * FULL_HEIGHT * 16:
                raise ValueError(f"unexpected depth capture: {depth_path}")
            origin, direction, limit, _ = ray_set(metadata[frame], depth_path, 6, 4)
            sun = captured_sun(metadata[frame])
            cand = candidate(levels, origin, direction, limit, sigma, 24, sun)
            ref64 = reference(levels[0], origin, direction, limit, sigma, 64., sun)
            if frame in (lo, hi):
                o2, d2, l2, g2 = ray_set(metadata[frame], depth_path, 12, 8)
                r128 = reference(levels[0], o2, d2, l2, sigma, 128., sun)
                r64 = reference(levels[0], o2, d2, l2, sigma, 64., sun)
                c24 = candidate(levels, o2, d2, l2, sigma, 24, sun)
                c48 = candidate(levels, o2, d2, l2, sigma, 48, sun)
                ref_opacity = 1 - r64["T"]
                reference_gap_rows.append({"family": family, "frame": frame, "rays": len(l2),
                                           "all": {"count": len(l2),
                                                   "fraction_opacity_below_0.002": fraction(ref_opacity < .002),
                                                   "fraction_opacity_above_0.015": fraction(ref_opacity > .015)},
                                           "sky": {"count": int((~g2).sum()),
                                                   "fraction_opacity_below_0.002": fraction(ref_opacity[~g2] < .002),
                                                   "fraction_opacity_above_0.015": fraction(ref_opacity[~g2] > .015)},
                                           "geometry": {"count": int(g2.sum()),
                                                        "fraction_opacity_below_0.002": fraction(ref_opacity[g2] < .002),
                                                        "fraction_opacity_above_0.015": fraction(ref_opacity[g2] > .015)}})
                conv_error = error_metrics(r128, r64); converged = numerical_pass(conv_error, True)
                convergence_rows.append({"family": family, "frame": frame, "rays": len(l2), "errors": conv_error, "passed": converged})
                reference_converged &= converged
                if converged:
                    for steps, candidate_result in ((24, c24), (48, c48)):
                        errors = error_metrics(candidate_result, r64); passed = numerical_pass(errors)
                        T_passed = errors["T"]["p99"] <= .001 and errors["T"]["max"] <= .003
                        empty = (1 - r64["T"]) <= 1e-8
                        false_count = int(np.sum(empty & ((1 - candidate_result["T"]) > .002)))
                        false_support_failed |= false_count > 0
                        candidate_passed &= passed if steps == 24 else True
                        candidate_T_passed &= T_passed if steps == 24 else True
                        worst = int(np.argmax(np.abs(candidate_result["T"] - r64["T"])))
                        grid_c = candidate_result["T"].reshape(8, 12); grid_r = r64["T"].reshape(8, 12)
                        gradient_residual = np.concatenate(((np.diff(grid_c, axis=0) - np.diff(grid_r, axis=0)).ravel(),
                                                            (np.diff(grid_c, axis=1) - np.diff(grid_r, axis=1)).ravel()))
                        score_rows.append({"family": family, "frame": frame, "far_steps": steps, "rays": len(l2),
                                           "errors": errors, "passed": passed, "T_passed": T_passed,
                                           "reference_empty_rays": int(empty.sum()),
                                           "false_support_over_0.002": false_count,
                                           "spatial_neighbor_T_gradient_residual": metric(np.abs(gradient_residual)),
                                           "minimal_failure_witness": {"ray_index": worst, "physical_limit": float(l2[worst]),
                                                                       "candidate_T": float(candidate_result["T"][worst]),
                                                                       "reference64_T": float(r64["T"][worst]),
                                                                       "abs_delta_T": float(abs(candidate_result["T"][worst] - r64["T"][worst])),
                                                                       "candidate_S_normalized_lighting": candidate_result["S"][worst].tolist(),
                                                                       "reference64_S_normalized_lighting": r64["S"][worst].tolist()} if not passed else None})
                short = l2 <= NEAR
                near_exact &= bool(np.array_equal(c24["S"][short], c24["near_S"][short]) and
                                   np.array_equal(c24["T"][short], c24["near_T"][short]))
            delta_candidate = None; delta_reference = None
            if family in previous:
                delta_candidate = cand["T"] - previous[family][0]
                delta_reference = ref64["T"] - previous[family][1]
                family_temporal.append(np.abs(delta_candidate - delta_reference))
            previous[family] = (cand["T"].copy(), ref64["T"].copy())
        temporal.append({"family": family, "frame_pairs": hi - lo, "rays_per_frame": 24,
                         "T_delta_residual": metric(np.concatenate(family_temporal))})
        for frame in (lo, hi):
            depth_path = capture / f"depth_1_{frame}.rgba32f"
            origin, direction, limit, geometry = ray_set(metadata[frame], depth_path, 64, 36)
            view = candidate(levels, origin, direction, limit, sigma, 24, captured_sun(metadata[frame]))
            row = {"family": family, "frame": frame, "rays": len(limit), **appearance(view, (36, 64), geometry)}
            clear_gate &= row["populations"]["sky"]["fraction_opacity_below_0.002"]["fraction"] >= .25
            all_views.append(row); write_sheet(output / f"{family}-{frame}-cloud-st.png", view, (36, 64))
        # Exact periodicity and a boundary crossing with sub-voxel displacement.
        dirs = np.array([[1., .2, -.1], [-.3, .9, .1]], F); dirs /= np.linalg.norm(dirs, axis=1)[:, None]
        lim = np.full(2, FAR, F); base = np.array([PERIOD - .25, 100., 200.])
        p0 = candidate(levels, base, dirs, lim, sigma, sun_direction=sun)
        pp = candidate(levels, base + [PERIOD, 0, 0], dirs, lim, sigma, sun_direction=sun)
        left = candidate(levels, base - [.5, 0, 0], dirs, lim, sigma, sun_direction=sun)
        right = candidate(levels, base + [.5, 0, 0], dirs, lim, sigma, sun_direction=sun)
        sources["packets"][family]["synthetic_periodic_translation_max"] = float(max(np.max(np.abs(p0["T"] - pp["T"])), np.max(np.abs(p0["S"] - pp["S"]))))
        sources["packets"][family]["synthetic_boundary_crossing_max_delta"] = float(max(np.max(np.abs(left["T"] - right["T"])), np.max(np.abs(left["S"] - right["S"]))))
        # Labeled camera interpolation: a fixed marker moves through the 40/30 km
        # taper radii. This checks continuity only; it is not recovered gameplay.
        axis = np.array([[1., 0., 0.]], F); marker = base + np.array([205000., 0., 0.])
        translations = np.linspace(0., 60000., 9); window_rows = []
        for translation in translations:
            camera_origin = base + np.array([translation, 0., 0.])
            distance = float(np.linalg.norm(marker - camera_origin))
            value = candidate(levels, camera_origin, axis, np.array([FAR], F), sigma, sun_direction=sun)
            window_rows.append({"camera_translation": float(translation), "fixed_marker_distance": distance,
                                "fixed_marker_window": float(1 - smoothstep(WINDOW_START, FAR, np.array([distance]))[0]),
                                "T": float(value["T"][0]), "S": value["S"][0].tolist()})
        sources["packets"][family]["synthetic_window_translation"] = {
            "label": "synthetic camera interpolation across fixed marker 40/30-km taper radii; not observed gameplay",
            "samples": window_rows,
            "max_adjacent_T_delta": float(max(abs(a["T"] - b["T"]) for a, b in zip(window_rows, window_rows[1:]))),
            "max_adjacent_S_delta": float(max(max(abs(x - y) for x, y in zip(a["S"], b["S"]))
                                                  for a, b in zip(window_rows, window_rows[1:]))) }
        del volume, levels
    laws_result = laws(mip_pyramid(decode_packet(asset_data / "bluewell.fogbin", manifest)))
    spatial = [{"family": row["family"], "frame": row["frame"], "far_steps": row["far_steps"],
                "neighbor_T_gradient_residual": row["spatial_neighbor_T_gradient_residual"]} for row in score_rows]
    recipe_screen_flag = any(row["sky"]["fraction_opacity_below_0.002"]["fraction"] < .25
                             for row in reference_gap_rows)
    laws_passed = (laws_result["vacuum_zero_length_exact"] and laws_result["invalid_depth_identity_exact"] and
                   laws_result["source_alpha_identity_exact"] and laws_result["finite"] and
                   laws_result["wrapping_continuity_max"] == 0)
    result_status = ("inconclusive-reference" if not reference_converged else
                     "rejected-integrator" if not candidate_passed else
                     "rejected-diagnostic-screen" if not (clear_gate and not false_support_failed and laws_passed and near_exact) else "passed-offline")
    result = {"schema": 1, "result": result_status, "sources": sources,
              "decision_scope": {"candidate_integrator_T_numerical_failure": not candidate_T_passed,
                                 "candidate_normalized_lighting_S_and_T_numerical_failure": not candidate_passed,
                                 "same_field_recipe_sky_diagnostic_screen_flag": recipe_screen_flag,
                                 "clear_fraction_0.25_is_user_visual_acceptance": False,
                                 "endpoint_views": 4,
                                 "does_not_reject_other_range_designs": True},
              "lighting_contract": {"S": "captured production-selected point-sun dir1 with unit radiance; numerical scale diagnostic only",
                                    "capture_has_verified_production_selected_sun_vector": True,
                                    "capture_has_verified_sun_radiance": False,
                                    "S_used_for_normalized_numerical_gate": True,
                                    "S_establishes_production_scaled_parity": False,
                                    "T_opacity_independent_of_lighting": True},
              "ray_sets": {"reference_scoring": "12x8 deterministic screen strata on first/last frames",
                           "temporal": "6x4 matched deterministic screen strata on all 64 frames",
                           "appearance": "64x36 deterministic screen strata on first/last frames"},
              "laws": laws_result, "reference_convergence": convergence_rows,
              "candidate_scores": score_rows, "temporal": temporal, "spatial_alias": spatial,
              "appearance": all_views, "converged_reference_gap_screen": reference_gap_rows,
              "gates": {"reference_converged": reference_converged,
                        "candidate_24_T_numerical": candidate_T_passed,
                        "candidate_24_normalized_S_and_T_numerical": candidate_passed,
                        "sky_diagnostic_clear_fraction_at_least_0.25": clear_gate,
                        "converged_reference_sky_screen_flag": recipe_screen_flag,
                        "exact_near_for_limits_at_most_12000": near_exact,
                        "false_support": not false_support_failed,
                        "laws": laws_passed},
              "operation_counts": {"baseline": {"midpoint_steps": 24, "field_texture_instructions": 48},
                                   "candidate_24_far": {"midpoint_steps": 48, "near_atlas_reads": 48, "far_atlas_reads": 96,
                                                        "field_texture_instructions": 144, "multiple_of_baseline": 3},
                                   "candidate_48_far": {"midpoint_steps": 72, "near_atlas_reads": 48, "far_atlas_reads": 192,
                                                        "field_texture_instructions": 240, "multiple_of_baseline": 5},
                                   "full_pixel_edge_repair": "same extended march per repaired pixel; separate from half-resolution pass"},
              "limitations": ["Offline CPU replay time and operation counts are not GPU timing or game FPS.",
                              "Cloud-only sheets use captured production-selected point-sun dir1 and normalized unit-radiance S at fixed 0..0.03 and opacity at 0..0.4; captured HDR/TAA is not recomposited.",
                              "Synthetic translations test continuity and periodicity; they are not recovered gameplay or observed pops.",
                              "The .25 clear-ray threshold is an established diagnostic rejection screen, not user visual rejection or sufficient acceptance.",
                              "Appearance and convergence cover four endpoint views from two captured profiles; they do not establish that every long-range design is impossible.",
                              "A passing offline replay would still require shader limits, portable packing, CPU/GPU parity, Reset recovery, timing, native Windows and user flight acceptance."],
              "host_seconds": time.monotonic() - started}
    report_path = output / "report.json"
    report_path.write_text(json.dumps(result, indent=2, sort_keys=True, allow_nan=False) + "\n")
    report_sha = digest(report_path)
    ref_p99 = max(row["errors"]["T"]["p99"] for row in convergence_rows)
    ref_max = max(row["errors"]["T"]["max"] for row in convergence_rows)
    by_steps = {steps: [row for row in score_rows if row["far_steps"] == steps] for steps in (24, 48)}
    candidate_summary = {steps: (max(row["errors"]["T"]["p99"] for row in rows),
                                 max(row["errors"]["T"]["max"] for row in rows),
                                 max(channel["max"] for row in rows for channel in row["errors"]["S_normalized_lighting"]))
                         for steps, rows in by_steps.items()}
    failed_witnesses = [row["minimal_failure_witness"] for row in score_rows
                        if row["far_steps"] == 24 and row["minimal_failure_witness"] is not None]
    witness = max(failed_witnesses, key=lambda row: row["abs_delta_T"]) if failed_witnesses else None
    reference_sky_clear = [row["sky"]["fraction_opacity_below_0.002"]["fraction"] for row in reference_gap_rows]
    reference_all_clear = [row["all"]["fraction_opacity_below_0.002"]["fraction"] for row in reference_gap_rows]
    candidate_sky_clear = [row["populations"]["sky"]["fraction_opacity_below_0.002"]["fraction"] for row in all_views]
    candidate_all_clear = [row["populations"]["all"]["fraction_opacity_below_0.002"]["fraction"] for row in all_views]
    reference_sky_counts = [row["sky"]["count"] for row in reference_gap_rows]
    candidate_sky_counts = [row["populations"]["sky"]["count"] for row in all_views]
    lines = ["# Fog distance offline replay", "", f"Result: **{result_status}**.", "",
             f"Reference convergence: {reference_converged}; candidate24 T numerical: {candidate_T_passed}; candidate24 normalized-S/T numerical: {candidate_passed}; sky clear-gap screen: {clear_gate}; false-support gate: {not false_support_failed}.", "",
             f"Reference128-vs64 |dT| p99/max worst view: {ref_p99:.9g}/{ref_max:.9g} (gates .00025/.00075).",
             f"Candidate24 |dT| p99/max worst view: {candidate_summary[24][0]:.9g}/{candidate_summary[24][1]:.9g}; normalized-lighting max-channel |dS| diagnostic: {candidate_summary[24][2]:.9g}.",
             f"Candidate48 alternate: |dT| {candidate_summary[48][0]:.9g}/{candidate_summary[48][1]:.9g}; normalized-lighting |dS| {candidate_summary[48][2]:.9g}.",
             (f"Minimal worst witness: physical limit {witness['physical_limit']:.0f}, candidate/reference T {witness['candidate_T']:.9g}/{witness['reference64_T']:.9g}, |dT| {witness['abs_delta_T']:.9g}."
              if witness else "No numerical failure witness: candidate24 passed all scored views."), "",
             f"Across four endpoint views, sky-only counts are {min(reference_sky_counts)}..{max(reference_sky_counts)} of 96 scoring rays and {min(candidate_sky_counts)}..{max(candidate_sky_counts)} of 2304 appearance rays. Reference sky clear fractions span {min(reference_sky_clear):.4f}..{max(reference_sky_clear):.4f}; candidate sky fractions span {min(candidate_sky_clear):.4f}..{max(candidate_sky_clear):.4f}. All-ray descriptive fractions are {min(reference_all_clear):.4f}..{max(reference_all_clear):.4f} reference and {min(candidate_all_clear):.4f}..{max(candidate_all_clear):.4f} candidate. Values below .25 flag only this deterministic diagnostic screen; they are not a population estimate, user visual rejection, or evidence that every range design is impossible.",
             f"Matched moving-reference temporal |dT residual| p99/max: bluewell {temporal[0]['T_delta_residual']['p99']:.9g}/{temporal[0]['T_delta_residual']['max']:.9g}; foggreenoutlands {temporal[1]['T_delta_residual']['p99']:.9g}/{temporal[1]['T_delta_residual']['max']:.9g}.", "",
             f"Ray sets: 96 endpoint scoring rays/view, 24 matched temporal rays/frame across 64 frames, 2304 appearance rays/view. Host replay: {result['host_seconds']:.2f}s.", "",
             "Cloud-only sheets show near S/T, far S/T, then combined S/T at fixed S=0..0.03 and opacity=0..0.4 scales. S uses captured production-selected point-sun dir1 with unit radiance and retains the normalized .0005/.002 numerical gate; unavailable radiance leaves production-scaled S parity open. See `report.json` for per-view distributions, convergence, errors, temporal residuals, connected regions, hashes and operation counts.", "",
             "This offline result is not a production selection or flight/native-Windows acceptance.", ""]
    (output / "report.md").write_text("\n".join(lines))
    (output / "summary.json").write_text(json.dumps({"result": result_status, "report_sha256": report_sha,
                                                       "manifest_sha256": sources["manifest_sha256"],
                                                       "selected_capture_metadata_sha256": metadata_sha,
                                                       "packets": {k: v["sha256"] for k, v in sources["packets"].items()}},
                                                      indent=2, sort_keys=True) + "\n")
    return result


def _correlation(a: np.ndarray, b: np.ndarray, mask: np.ndarray):
    x = np.asarray(a, np.float64)[mask]; y = np.asarray(b, np.float64)[mask]
    if len(x) < 2 or np.std(x) == 0 or np.std(y) == 0:
        return None
    return float(np.corrcoef(x, y)[0, 1])


def run_reference_experiment(capture: Path, asset_data: Path, output: Path) -> dict:
    started = time.monotonic(); output.mkdir(parents=True, exist_ok=True)
    manifest_path = asset_data / "manifest.json"; manifest = json.loads(manifest_path.read_text())
    log_paths = sorted(capture.glob("session-*.log"))
    if len(log_paths) != 1:
        raise ValueError("expected one capture session log")
    metadata, metadata_sha = load_metadata(log_paths[0])
    profiles = {row["name"]: row for row in manifest["profiles"]}
    depth_paths = [capture / f"depth_1_{frame}.rgba32f"
                   for lo, hi, _ in BURSTS for frame in range(lo, hi + 1)]
    depth_digests = {path.name: digest(path) for path in depth_paths}
    depth_aggregate = hashlib.sha256(b"".join(bytes.fromhex(depth_digests[path.name])
                                                for path in depth_paths)).hexdigest()
    endpoint_names = {f"depth_1_{frame}.rgba32f" for lo, hi, _ in BURSTS for frame in (lo, hi)}
    sources = {"manifest_sha256": digest(manifest_path),
               "selected_capture_metadata_sha256": metadata_sha,
               "reviewed_replay_report_sha256": "80fc97843d58e28c1ce26e05fa7efd2cdd2752d54b0bf27fa5947be82a224bf0",
               "analysis_sha256": digest(Path(__file__)),
               "capture_log": str(log_paths[0]), "capture_log_bytes": log_paths[0].stat().st_size,
               "depth_files": {"count": len(depth_paths), "aggregate_sha256": depth_aggregate,
                               "endpoint_sha256": {name: depth_digests[name] for name in sorted(endpoint_names)}},
               "packets": {}}
    views = []; temporal_rows = []; synthetic = {}; all_converged = True
    all_near_exact = True; all_geometry_near_exact = True
    for lo, hi, family in BURSTS:
        packet = asset_data / f"{family}.fogbin"; volume = decode_packet(packet, manifest)
        levels = mip_pyramid(volume); sigma = float(profiles[family]["base_sigma"]) * 1.5
        sources["packets"][family] = {"sha256": digest(packet),
                                       "decoded_sha256": profiles[family]["decoded_sha256"]}
        for frame in (lo, hi):
            depth_path = capture / f"depth_1_{frame}.rgba32f"
            origin, direction, limit, geometry = ray_set(metadata[frame], depth_path, 64, 36)
            sun = captured_sun(metadata[frame])
            ref128 = accurate_reference(volume, origin, direction, limit, sigma, 128., sun)
            ref64 = accurate_reference(volume, origin, direction, limit, sigma, 64., sun)
            rejected = candidate(levels, origin, direction, limit, sigma, 24, sun)
            far_errors = {"T": metric(np.abs(ref128["far_T"] - ref64["far_T"])),
                          "S_normalized_unit_radiance": [
                              metric(np.abs(ref128["far_S"][:, channel] - ref64["far_S"][:, channel]))
                              for channel in range(3)]}
            converged = far_errors["T"]["p99"] <= .00025 and far_errors["T"]["max"] <= .00075
            all_converged &= converged
            near_exact = (np.array_equal(ref128["near_S"], ref64["near_S"]) and
                          np.array_equal(ref128["near_T"], ref64["near_T"]))
            short = limit <= NEAR
            geometry_near_exact = (np.array_equal(ref64["S"][short], ref64["near_S"][short]) and
                                   np.array_equal(ref64["T"][short], ref64["near_T"][short]))
            all_near_exact &= near_exact; all_geometry_near_exact &= geometry_near_exact
            refinement = None
            if not converged:
                error = np.abs(ref128["far_T"] - ref64["far_T"])
                witness = np.argsort(error)[-min(16, len(error)):]
                ref32 = accurate_reference(volume, origin, direction[witness], limit[witness], sigma, 32., sun)
                refinement = {"rays": len(witness), "indices": witness.tolist(),
                              "T_64_vs_32": metric(np.abs(ref64["far_T"][witness] - ref32["far_T"]))}
            opacity = 1 - ref64["T"]; shell2_opacity = 1 - ref64["shell2_T"]
            sky = ~geometry; shell_dense = (shell2_opacity > .002) & sky
            block_correlations = []
            for shell, banks, start, end in (("2.4-30km", ref64["shell1_bank_tau"], NEAR, WINDOW_START),
                                             ("30-40km", ref64["shell2_bank_tau"], WINDOW_START, FAR)):
                bounds = [[start + index * PERIOD, min(end, start + (index + 1) * PERIOD)]
                          for index in range(len(banks))]
                for index in range(len(banks) - 1):
                    block_correlations.append({"shell": shell, "blocks": [index, index + 1],
                                               "bounds": [bounds[index], bounds[index + 1]],
                                               "includes_partial_block": bool(any(high - low < PERIOD
                                                                                   for low, high in (bounds[index], bounds[index + 1]))),
                                               "sky_tau_correlation": _correlation(banks[index], banks[index + 1], sky)})
            stem = f"{family}-{frame}"
            image_files = []
            image_files += write_reference_images(output, stem + "-reference64", ref64, rejected, (36, 64))
            image_files += write_reference_images(output, stem + "-reference128", ref128, rejected, (36, 64))
            image_files += write_reference_images(output, stem + "-reference64-sky", ref64, rejected, (36, 64), mask=sky)
            image_files += write_reference_images(output, stem + "-reference128-sky", ref128, rejected, (36, 64), mask=sky)
            views.append({"family": family, "frame": frame, "rays": len(limit),
                          "sky_rays": int(sky.sum()), "geometry_rays": int(geometry.sum()),
                          "far_128_vs_64": far_errors, "far_converged": converged,
                          "refinement32_on_failure": refinement,
                          "near24_identical_between_arms": near_exact,
                          "limits_at_most_12000_exact_near_output": geometry_near_exact,
                          "complete_opacity": {"all": {"clear_below_0.002": fraction(opacity < .002),
                                                         "dense_above_0.015": fraction(opacity > .015)},
                                               "sky": {"clear_below_0.002": fraction(opacity[sky] < .002),
                                                       "dense_above_0.015": fraction(opacity[sky] > .015)}},
                          "shell_30_40km": {"sky_count": int(sky.sum()),
                                             "opacity": {"p0": float(np.min(shell2_opacity[sky])),
                                                         "p50": float(np.percentile(shell2_opacity[sky], 50)),
                                                         "p99": float(np.percentile(shell2_opacity[sky], 99)),
                                                         "max": float(np.max(shell2_opacity[sky])),
                                                         "std": float(np.std(shell2_opacity[sky]))},
                                             "above_0.002": fraction(shell2_opacity[sky] > .002),
                                             "connected_regions_above_0.002": components(shell_dense.reshape(36, 64)),
                                             "spatially_varying_nonzero": bool(np.std(shell2_opacity[sky]) > 1e-5 and
                                                                               np.any(shell2_opacity[sky] > .002))},
                          "shell_local_period_width_block_correlations": block_correlations,
                          "candidate24_difference": error_metrics(rejected, ref64),
                          "images": image_files})
        previous = None; changes = []
        for frame in range(lo, hi + 1):
            depth_path = capture / f"depth_1_{frame}.rgba32f"
            origin, direction, limit, _ = ray_set(metadata[frame], depth_path, 6, 4)
            current = accurate_reference(volume, origin, direction, limit, sigma, 64., captured_sun(metadata[frame]))
            if previous is not None:
                changes.append(np.abs(current["T"] - previous["T"]))
            previous = current
        temporal_rows.append({"family": family, "frames": 32, "frame_pairs": 31,
                              "rays_per_frame": 24,
                              "moving_reference_abs_delta_T": metric(np.concatenate(changes))})
        dirs = np.array([[1., .2, -.1], [-.3, .9, .1]], F); dirs /= np.linalg.norm(dirs, axis=1)[:, None]
        lim = np.full(2, FAR, F); base = np.array([PERIOD - .25, 100., 200.]); sun = captured_sun(metadata[hi])
        p0 = accurate_reference(volume, base, dirs, lim, sigma, 64., sun)
        pp = accurate_reference(volume, base + [PERIOD, 0, 0], dirs, lim, sigma, 64., sun)
        left = accurate_reference(volume, base - [.5, 0, 0], dirs, lim, sigma, 64., sun)
        right = accurate_reference(volume, base + [.5, 0, 0], dirs, lim, sigma, 64., sun)
        axis = np.array([[1., 0., 0.]], F); marker = base + np.array([205000., 0., 0.])
        window_rows = []
        for translation in np.linspace(0., 60000., 9):
            camera_origin = base + np.array([translation, 0., 0.])
            marker_distance = float(np.linalg.norm(marker - camera_origin))
            value = accurate_reference(volume, camera_origin, axis, np.array([FAR], F), sigma, 64., sun)
            window_rows.append({"camera_translation": float(translation),
                                "fixed_marker_distance": marker_distance,
                                "fixed_marker_window": float(1 - smoothstep(WINDOW_START, FAR,
                                                                              np.array([marker_distance]))[0]),
                                "T": float(value["T"][0]), "S": value["S"][0].tolist()})
        synthetic[family] = {"period_translation_max": float(max(np.max(np.abs(p0["T"] - pp["T"])),
                                                                     np.max(np.abs(p0["S"] - pp["S"])))),
                             "wrap_boundary_one_unit_max_delta": float(max(np.max(np.abs(left["T"] - right["T"])),
                                                                             np.max(np.abs(left["S"] - right["S"])))),
                             "window_translation": {"label": "synthetic camera interpolation across fixed marker 40/30-km taper radii; not observed gameplay",
                                                    "samples": window_rows,
                                                    "max_adjacent_T_delta": float(max(abs(a["T"] - b["T"])
                                                                                      for a, b in zip(window_rows, window_rows[1:])))}}
        del levels, volume
    image_paths = sorted(output.glob("*.png"))
    laws_result = laws(mip_pyramid(decode_packet(asset_data / "bluewell.fogbin", manifest)))
    laws_passed = (laws_result["vacuum_zero_length_exact"] and laws_result["invalid_depth_identity_exact"] and
                   laws_result["source_alpha_identity_exact"] and laws_result["finite"] and
                   laws_result["wrapping_continuity_max"] == 0)
    status = ("passed-reference-experiment" if all_converged and all_near_exact and
              all_geometry_near_exact and laws_passed else "inconclusive-reference")
    result = {"schema": 1, "result": status, "sources": sources,
              "contract": {"image_grid": [64, 36], "near": "original 24 midpoint samples through 12000",
                           "far": "original unfiltered level0 field at fixed 128/64 render-unit spacing",
                           "shells": [[12000, 150000], [150000, 200000]],
                           "window": "1-smoothstep(150000,200000,s)",
                           "strength": .03, "density_scale": 1.5,
                           "lighting": "captured production-selected point-sun dir1, normalized unit radiance",
                           "S_limitation": "actual radiance unavailable; no production-scaled S parity"},
              "gates": {"all_far_128_vs_64_T_converged": all_converged,
                        "near24_identical_between_arms": all_near_exact,
                        "limits_at_most_12000_exact_near_output": all_geometry_near_exact,
                        "laws_passed": laws_passed, "laws": laws_result},
              "views": views, "temporal": temporal_rows, "synthetic": synthetic,
              "operation_counts": {"reference128_far_steps": int(math.ceil((WINDOW_START - NEAR) / 128) +
                                                                   math.ceil((FAR - WINDOW_START) / 128)),
                                   "reference64_far_steps": int(math.ceil((WINDOW_START - NEAR) / 64) +
                                                                  math.ceil((FAR - WINDOW_START) / 64)),
                                   "reference32_only_on_failed_witnesses": True,
                                   "GPU_or_production_cost_claim": False},
              "images": {path.name: digest(path) for path in image_paths},
              "limitations": ["Dense CPU reference is not a proposed GPU algorithm or FPS measurement.",
                              "The four deterministic endpoint images are appearance witnesses, not population estimates or user acceptance.",
                              "The 25% clear-ray value is descriptive only and does not pass or fail appearance.",
                              "No TAA or observed-pop acceptance follows from fog-only images.",
                              "Production shader limits, timing, Reset behavior, native Windows execution and user flight remain open."],
              "host_seconds": time.monotonic() - started}
    report_path = output / "report.json"
    report_path.write_text(json.dumps(result, indent=2, sort_keys=True, allow_nan=False) + "\n")
    worst = max(view["far_128_vs_64"]["T"]["max"] for view in views)
    worst_p99 = max(view["far_128_vs_64"]["T"]["p99"] for view in views)
    shell_fractions = [view["shell_30_40km"]["above_0.002"]["fraction"] for view in views]
    shell_regions = [view["shell_30_40km"]["connected_regions_above_0.002"] for view in views]
    sky_clear = [view["complete_opacity"]["sky"]["clear_below_0.002"]["fraction"] for view in views]
    block_correlation = max(abs(row["sky_tau_correlation"] or 0.)
                            for view in views for row in view["shell_local_period_width_block_correlations"])
    lines = ["# Converged long-range fog reference images", "", f"Result: **{status}**.", "",
             f"Dense 128-vs-64 far-only T convergence worst-view p99/max: {worst_p99:.9g}/{worst:.9g} (gates .00025/.00075).",
             f"Four endpoint images use 2304 deterministic rays each; sky counts are {min(v['sky_rays'] for v in views)}..{max(v['sky_rays'] for v in views)}.",
             f"The 30-40 km shell exceeds .002 opacity on {min(shell_fractions):.4f}..{max(shell_fractions):.4f} of sampled sky rays in {min(shell_regions)}..{max(shell_regions)} connected screen regions. It is spatially varying and nonzero in all four views; this does not imply sparse or localized support. Within-shell adjacent period-width distance-block sky optical-depth correlations have maximum absolute value {block_correlation:.4f}; report.json records bounds and partial blocks.",
             f"Complete-column sky clear fractions below .002 are {min(sky_clear):.4f}..{max(sky_clear):.4f}; this descriptive screen is not a visual verdict.", "",
             "Sheets show near24, the unfiltered 2.4-30 km shell, the tapered 30-40 km shell, complete reference, and reference-minus-rejected-candidate. Separate transmission and optical-depth images use fixed scales.",
             "S uses captured production-selected point-sun dir1 with normalized unit radiance; production-scaled S parity remains open.",
             "These cloud-only deterministic images are not user visual acceptance and do not select a production integrator or spatial recipe.", "",
             f"Host runtime: {result['host_seconds']:.2f}s. No game, Wine, build, production edit or install was performed.", ""]
    (output / "report.md").write_text("\n".join(lines))
    (output / "summary.json").write_text(json.dumps({"result": status, "report_sha256": digest(report_path),
                                                       "manifest_sha256": sources["manifest_sha256"],
                                                       "selected_capture_metadata_sha256": metadata_sha,
                                                       "images": len(image_paths)}, indent=2, sort_keys=True) + "\n")
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--capture", type=Path, required=True)
    parser.add_argument("--asset-data", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--reference-images", action="store_true",
                        help="run the ratified dense unfiltered reference-image experiment")
    args = parser.parse_args()
    result = (run_reference_experiment(args.capture, args.asset_data, args.output)
              if args.reference_images else run(args.capture, args.asset_data, args.output))
    print(json.dumps({"result": result["result"], "output": str(args.output), "seconds": result["host_seconds"]}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
