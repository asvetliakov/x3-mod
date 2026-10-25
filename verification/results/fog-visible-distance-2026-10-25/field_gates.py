#!/usr/bin/env python3
"""Host gates of the long distance fade on the frozen reference field (docs/architecture/fog-visible-distance.md §3).

The look law of tools/analysis/fog_density_shader_reference.py (`look_march`, `distance=`) over the runtime screen's
LazyStore (tools/analysis/fog_density_runtime_screen.py: the frozen field, the 8-point node prefilter, fine 512 / far 4096
and here the horizon level at 8192) on the screen's two poses. No GPU, no game. Prints, per law (today's 22.5 km
smoothstep; fog_distance 3 with the horizon level ready; fog_distance 3 without it, the 39 km fallback):
  - sky veil: 1-T mean / median / p99 and the exactly-empty share of the sky rays (gate: median <= .02, empty >= .40);
  - motion shimmer: the largest |dT| on a sky ray when the camera moves 250 units forward (the camera-relative bins slide
    through the field), today's law against the new one (gate: max no worse than today's);
  - approach (field): the camera flies 60 km forward in 500-unit steps; per ray the largest change of 1-T per km of
    travel, max and p99 over the rays. Whole-sky: near-camera cloud passes dominate it, so it is reported, not gated;
  - bank approach (the design's gate): one isolated bank (2 km radius, stored density .9) straight ahead, approached
    from 80 km to 14 km in 100-unit steps on the central ray; the largest change of 1-T over 1 km of travel (a 1 km
    difference: single 100-unit steps measure the bin quantisation, about .03 per bin crossing in both laws) while the
    bank is farther than 15 km, and the distances where 1-T first exceeds .02 and half its value at 15 km
    (gate: largest change <= 1/3 of today's);
  - horizon node law: fog_distance 3 with the horizon nodes of the shipped 8-point prefilter (offsets +-8192/4) against
    a box reference (each horizon node the mean of the 2x2x2 far nodes it covers); |dT| on the sky rays, p99 and max
    (gate: p99 <= .003).
usage: python3 -I field_gates.py [--rays N] [--json OUT]   (sky rays per pose: a stride subset of the 128x72 screen)
"""
import argparse
import importlib.util
import json
import sys
import time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ref = load('fog_density_shader_reference', ROOT / 'tools/analysis/fog_density_shader_reference.py')
m = ref.m
m.LEVELS['horizon'] = 8192.  # the third level (kHorizonDelta); the store's window law is the same 128 nodes


def march(origin, rays, law, store):
    chroma = (0.20072728, 1.0, 0.120704934)
    n = len(rays)
    kwargs = {} if law == 'today' else dict(distance=3., horizon=law == 'x3', horizon_ready=1. if law == 'x3' else 0.)
    return ref.look_march(origin, rays, np.full(n, m.FAR), False, chroma, store, m.SIGMA, **kwargs)[1].astype(np.float64)


class Bank:
    """One spherical bank of stored density .9 (smooth 500-unit edge) centred `centre` render units ahead on +x."""
    def __init__(self, centre, radius=10000.):
        self.centre, self.radius = centre, radius

    def value(self, points):
        r = np.linalg.norm(np.asarray(points, np.float64) - np.array([self.centre, 0., 0.]), axis=1)
        return (.9 * np.clip((self.radius - r) / 500., 0., 1.)).astype(np.float32)

    def sample(self, points, distance, camera, counts=None):
        return self.value(points)

    def sample_level(self, name, points, camera):
        return self.value(points)


class BoxStore(m.LazyStore):
    """Horizon nodes as the mean of the 8 far-level nodes (each the 8-point far prefilter) inside the horizon node's
    footprint: the box reference of the design's aliasing screen. The other levels are the shipped law."""
    def get(self, name, keys):
        if name != 'horizon':
            return super().get(name, keys)
        k = np.asarray(keys, np.int64).reshape(-1, 3)
        acc = np.zeros(len(k), np.float64)
        for dz in (0, 1):
            for dy in (0, 1):
                for dx in (0, 1):
                    acc += super().get('far', 2 * k + np.array([dx, dy, dz])).astype(np.float64)
        return (acc / 8).astype(np.float16).reshape(np.asarray(keys).shape[:-1])


def bank_approach(law):
    centre = 400000.
    bank = Bank(centre)
    camera = np.arange(0., centre - 70000. + 1., 100.)  # bank distance 80 km .. 14 km
    veil = np.array([1 - march(np.array([c, 0., 0.]), np.array([[1., 0., 0.]]), law, bank)[0] for c in camera])
    distance_km = (centre - camera) / 5000.
    rate = np.abs(veil[50:] - veil[:-50])  # over 1 km of travel (50 steps of 100 units)
    far = distance_km[50:] > 15.
    at15 = veil[np.argmin(np.abs(distance_km - 15.))]
    seen = distance_km[veil > .02]
    half = distance_km[veil > .5 * at15]
    return dict(max_rate_per_km=float(rate[far].max()), veil_at_15km=float(at15),
                visible_from_km=float(seen.max()) if len(seen) else None, half_from_km=float(half.max()) if len(half) else None)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--rays', type=int, default=8, help='stride of the 128x72 sky-ray subset')
    p.add_argument('--steps', type=int, default=120, help='approach steps of 500 units')
    p.add_argument('--json', type=Path)
    a = p.parse_args()
    store = m.LazyStore()
    box_store = BoxStore()
    out = {}
    laws = ('today', 'x3', 'x3_no_horizon')
    for pose in m.screen.pose_rows():
        origin = np.asarray(pose['origin'], np.float64); forward = np.asarray(pose['forward'], np.float64)
        all_rays = m.rays(pose).astype(np.float64)
        index = np.array([y * m.W + x for y in range(a.rays // 2, m.H, a.rays) for x in range(a.rays // 2, m.W, a.rays)])
        rays = all_rays[index]
        row = {'rays': int(len(rays))}
        start = time.monotonic()
        for law in laws:
            T = march(origin, rays, law, store)
            veil = 1 - T
            moved = march(origin + 250. * forward, rays, law, store)
            row[law] = dict(veil_mean=float(veil.mean()), veil_median=float(np.median(veil)), veil_p99=float(np.percentile(veil, 99)),
                            empty_share=float(np.mean(T == 1.)), shimmer_max=float(np.abs(moved - T).max()),
                            shimmer_p99=float(np.percentile(np.abs(moved - T), 99)))
        # The approach: the same rays from positions along the forward axis.
        sweep = {law: [] for law in ('today', 'x3')}
        for k in range(a.steps + 1):
            o = origin + 500. * k * forward
            for law in sweep:
                sweep[law].append(1 - march(o, rays, law, store))
        for law, series in sweep.items():
            rate = np.abs(np.diff(np.stack(series), axis=0)) / .1  # per km (500 units = .1 km)
            per_ray = rate.max(axis=0)
            row[law]['approach_max_per_km'] = float(per_ray.max())
            row[law]['approach_p99_per_km'] = float(np.percentile(per_ray, 99))
        box = march(origin, rays, 'x3', box_store)
        shipped = march(origin, rays, 'x3', store)
        diff = np.abs(box - shipped)
        row['horizon_box_reference'] = dict(T_p99=float(np.percentile(diff, 99)), T_max=float(diff.max()))
        row['seconds'] = round(time.monotonic() - start, 1)
        if pose is m.screen.pose_rows()[0] or not out:
            for law in ('today', 'x3'):
                row[law]['bank'] = bank_approach(law)
        out[pose['name']] = row
        print(pose['name'], json.dumps(row))
    gates = {}
    for name, row in out.items():
        x3, today = row['x3'], row['today']
        gates[name] = dict(horizon_box=row['horizon_box_reference']['T_p99'] <= .003, veil_median=x3['veil_median'] <= .02, empty_share=x3['empty_share'] >= .40,
                           shimmer=x3['shimmer_max'] <= today['shimmer_max'] + 1e-6,
                           bank_approach=x3['bank']['max_rate_per_km'] <= today['bank']['max_rate_per_km'] / 3 if 'bank' in x3 else None)
    print('GATES', json.dumps(gates))
    if a.json:
        a.json.write_text(json.dumps(dict(method=__doc__.splitlines()[0], stride=a.rays, steps=a.steps, poses=out, gates=gates), indent=1) + '\n')


if __name__ == '__main__':
    sys.exit(main())
