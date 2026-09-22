#!/usr/bin/env python3
"""Shaft lookup sampling of the fog look law against a flight's dumped shadow maps (host only, no GPU).

Marches `look_march` of fog_density_shader_reference.py over a burst of a capture directory: rays from the logged
`camera_state`, shaft visibility from `shadow_map2/3_1_<frame>.r32f` (the two cascades the look programs bind) through
the logged `shadow_replay_map_basis`, with the fog_look_visibility law (hard switch at .85, one 2x2 comparison).
Density is either uniform (the run222 diagnosis proxy) or the analytic family field at the screen's pose A (cloud
structure for the grain figures; the flight's stored field is not reconstructed).

Variants: shaft lookup at bin centres (L2 before 2026-09-22), the lookup offset per pixel and TAA phase (shadow
jitter, density at centres), L3 (every sample offset), two lookups per bin at +-1/4 bin with and without the offset.
Reference: density at bin centres, 16 lookups per bin. Errors are (S - S_ref) / S_lit of the green channel, S_lit the
same march without shadows. Grain is the temporal std/mean of the raw 8-phase cycle and after an exponential history
of weight .9 (the TAA default) at a held camera.
"""
from __future__ import annotations
import argparse, json, re, subprocess, sys
from pathlib import Path
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fog_density_shader_reference as ref  # noqa: E402

CHROMA = (0.20072728, 1.0, 0.120704934)
SIGMA = 3.75e-6  # family sigma x 1.5 strength, as the shader fixture


class Field:
    """look_march store: the analytic family field (unfiltered) or a constant."""
    def __init__(self, uniform): self.uniform = uniform; self.memo = {}
    def value(self, points):
        if self.uniform:
            return np.full(len(points), .761, np.float32)  # remapped to .4: sigma 1.2e-5 per unit, the diagnosis' proxy
        key = hash(np.ascontiguousarray(points).tobytes())  # the bin-centre variants repeat the same positions
        if key not in self.memo:
            self.memo[key] = ref.m.field(points)
        return self.memo[key]
    def sample(self, points, distance, camera, counts=None): return self.value(points)
    def sample_level(self, name, points, camera): return self.value(points)


def log_rows(log, frame):
    text = subprocess.run(['grep', '-E', rf'^(camera_state|shadow_replay_map_basis) device=1 frame={frame} ', str(log)], capture_output=True, text=True, check=True).stdout
    number = r'(-?[0-9.eE+-]+)'
    cam = next(line for line in text.splitlines() if line.startswith('camera_state'))
    value = lambda line, key: float(re.search(rf' {key}={number}', line).group(1))
    R = np.array([[value(cam, f'r{i}{j}') for j in range(3)] for i in range(3)]); t = np.array(re.search(rf' t={number},{number},{number}', cam).groups(), float)
    camera = dict(R=R, position=-R @ t, p00=value(cam, 'p00'), p11=value(cam, 'p11'))
    cascades = {}
    for line in text.splitlines():
        if line.startswith('shadow_replay_map_basis'):
            vec = lambda key: np.array(re.search(rf' {key}={number},{number},{number}', line).groups(), float)
            cascades[int(value(line, 'cascade'))] = dict(right=vec('right'), up=vec('up'), forward=vec('forward'), center=vec('center'), extent=value(line, 'extent'),
                                                         light=value(line, 'depth_light'), behind=value(line, 'depth_behind'), size=int(value(line, 'size')))
    return camera, cascades, -cascades[3]['forward']


def map_visibility(camera, cascades, maps, bias_units, taps=(0.,), seen=None):
    """fog_look_visibility over cascades 2 and 3; `taps` are lookup offsets along the ray in bins (mean of the taps)."""
    def one(world):
        vis = np.ones(len(world)); free = np.ones(len(world), bool)
        for index in (2, 3):
            c = cascades[index]; q = world - c['center']
            x = np.dot(q, c['right']) / c['extent']; y = np.dot(q, c['up']) / c['extent']; z = (np.dot(q, c['forward']) + c['light']) / (c['light'] + c['behind'])
            inside = free & (np.maximum(np.abs(x), np.abs(y)) <= .85) & (z >= 0) & (z <= 1); free &= ~inside
            n = c['size']; tx = (x[inside] * .5 + .5) * n; ty = (-y[inside] * .5 + .5) * n
            bx = np.floor(tx).astype(int); by = np.floor(ty).astype(int); fx = tx - bx; fy = ty - by
            lit = lambda i, j: maps[index][np.clip(j, 0, n - 1), np.clip(i, 0, n - 1)] >= (z[inside] - bias_units / (c['light'] + c['behind']))
            vis[inside] = (lit(bx, by) * (1 - fx) + lit(bx + 1, by) * fx) * (1 - fy) + (lit(bx, by + 1) * (1 - fx) + lit(bx + 1, by + 1) * fx) * fy
        return vis

    def visibility(points, rays, ds):
        direction = points / np.linalg.norm(points, axis=1, keepdims=True)
        vis = sum(one(camera['position'] + points + direction * (tap * ds)[:, None]) for tap in taps) / len(taps)
        if seen is not None:  # lowest visibility any lookup of the ray returned
            np.minimum.at(seen, rays, vis)
        return vis
    return visibility


def stats(x):
    x = np.asarray(x, np.float64).ravel()
    if not x.size:
        return None
    return dict(rms=float(np.sqrt((x * x).mean())), p50=float(np.percentile(np.abs(x), 50)), p99=float(np.percentile(np.abs(x), 99)), max=float(np.abs(x).max()))


def grain(frames, mask, history=.9, warm=64):
    """Temporal std/mean of the raw phase cycle and of an exponential history over it (held camera)."""
    if not mask.any():
        return None
    frames = np.asarray(frames)[:, mask]; raw = frames.std(0) / frames.mean(0)
    h = frames[0].copy(); kept = []
    for i in range(warm + 2 * len(frames)):
        h = history * h + (1 - history) * frames[i % len(frames)]
        if i >= warm:
            kept.append(h.copy())
    resolved = np.std(kept, 0) / np.mean(kept, 0)
    return {name: dict(median=float(np.median(v)), p99=float(np.percentile(v, 99)), max=float(v.max())) for name, v in (('raw', raw), ('after_history', resolved))}


def study(directory, frame, width, height, uniform, bias_units, field_origin, phases=8):
    log = next(Path(directory).glob('session-*.log')); camera, cascades, sun = log_rows(log, frame)
    maps = {i: np.fromfile(Path(directory) / f'shadow_map{i}_1_{frame}.r32f', np.float32).reshape(cascades[i]['size'], -1) for i in (2, 3)}
    x, y = np.meshgrid(np.arange(width), np.arange(height)); x = x.ravel(); y = y.ravel()
    view = np.stack(((2 * (x + .5) / width - 1) / camera['p00'], (1 - 2 * (y + .5) / height) / camera['p11'], np.ones(len(x))), 1)
    d = np.einsum('ij,kj->ik', view, camera['R']); d /= np.linalg.norm(d, axis=1, keepdims=True)
    origin = np.zeros(3) if uniform else np.asarray(field_origin, np.float64); store = Field(uniform)
    # Scale the half-resolution pixel grid so the interleaved-gradient pattern is the flight's (960x540 half target).
    pixels = (np.round(x * 960 / width), np.round(y * 540 / height))

    met = np.ones(len(d))  # lowest shaft visibility any bin-centre or offset lookup (every phase) returned per ray

    def march(phase=0, jitter=1., taps=(0.,), lit=False, seen=None):
        S, _ = ref.look_march(origin, d, np.full(len(d), 2e5), False, CHROMA, store, SIGMA, phase, pixels, sun=sun,
                              visibility=None if lit else map_visibility(camera, cascades, maps, bias_units, taps, seen), tuning=dict(ref.TUNING, shadow_jitter=jitter))
        return S[:, 1].astype(np.float64)
    np.seterr(divide='ignore', invalid='ignore')  # masked-out rays
    lit = march(lit=True); keep = lit > .02 * lit.max()
    dense = march(jitter=0., taps=tuple((j + .5) / 16 - .5 for j in range(16)))
    shaft = keep & (dense < .995 * lit); clear = keep & (dense >= lit * (1 - 1e-9))
    error = lambda S, mask=keep: stats(((S - dense) / lit)[mask])
    centres = march(jitter=0., seen=met)
    jittered = [march(p, seen=met) for p in range(phases)]
    two = march(jitter=0., taps=(-.25, .25)); two_jittered = [march(p, jitter=.5, taps=(-.25, .25)) for p in range(phases)]
    out = dict(frame=frame, rays=[width, height], density='uniform' if uniform else 'analytic family field at %s' % list(map(float, origin)), fogged_rays=int(keep.sum()), shaft_rays=int(shaft.sum()), clear_rays=int(clear.sum()),
               comb_vs_dense=dict(bin_centres=error(centres), shadow_jitter_one_phase=error(jittered[0]), shadow_jitter_mean=error(np.mean(jittered, 0)),
                                  two_taps=error(two), two_taps_jitter_one_phase=error(two_jittered[0]), two_taps_jitter_mean=error(np.mean(two_jittered, 0))),
               grain_clear=dict(shadow_jitter=grain(jittered, clear)), grain_shaft=dict(shadow_jitter=grain(jittered, shaft), two_taps_jitter=grain(two_jittered, shaft)),
               # A repair pixel keeps bin centres beside half-resolution neighbours whose lookup is offset: its step against
               # them, relative to the local in-scatter, against one frame and against what the history converges to.
               repair_step_in_shaft=dict(vs_one_phase=stats(((centres - jittered[0]) / dense)[shaft]), vs_resolved=stats(((centres - np.mean(jittered, 0)) / dense)[shaft]),
                                         neighbour_one_phase_vs_resolved=stats(((jittered[0] - np.mean(jittered, 0)) / dense)[shaft])))
    # `clear` comes from the 16-lookup reference, which steps 1/16 bin (157 units in a far bin) and so misses thinner
    # occluders that an offset lookup can land on. The leak test is the second row: a ray none of whose offset lookups met
    # an occluder must equal the unshadowed march bit for bit (the offset reaches nothing but the lookup).
    grazed = clear & (met < 1.); unmet = keep & (met >= 1.)
    out['clear_rays_identical_to_centres'] = bool(all(np.array_equal(j[clear], centres[clear]) for j in jittered))
    out['clear_rays_where_an_offset_lookup_met_an_occluder'] = int(grazed.sum())
    out['clear_rays_differing_without_meeting_an_occluder'] = int(sum((j[clear & ~grazed] != centres[clear & ~grazed]).sum() for j in jittered))
    out['rays_no_offset_lookup_met_an_occluder'] = int(unmet.sum())
    out['those_rays_identical_to_unshadowed_march'] = bool(all(np.array_equal(j[unmet], lit[unmet]) for j in jittered))
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture', type=Path, required=True); p.add_argument('--frame', type=int, action='append', required=True)
    p.add_argument('--width', type=int, default=160); p.add_argument('--height', type=int, default=96)
    p.add_argument('--field-origin', type=lambda v: [float(x) for x in v.split(',')], default=[95576., 97323., 82698.], help='where the camera sits in the analytic field (default: screen pose A)')
    p.add_argument('--bias-units', type=float, default=8.); p.add_argument('--output', type=Path)
    a = p.parse_args()
    rows = [study(a.capture, frame, a.width, a.height, uniform, a.bias_units, a.field_origin) for frame in a.frame for uniform in (True, False)]
    text = json.dumps(dict(schema=1, tool='tools/analysis/fog_shaft_sampling_study.py', capture=str(a.capture), bias_units=a.bias_units, history_weight=.9, phases=8, studies=rows), indent=1)
    if a.output:
        a.output.write_text(text + '\n')
    print(text)


if __name__ == '__main__':
    main()
