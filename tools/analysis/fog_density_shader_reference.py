#!/usr/bin/env python3
"""Per-ray reference export for the stored-density shader fixture (checkpoint 2).

Imports tools/analysis/fog_density_runtime_screen.py unchanged (its digest is pinned by the
screen's tracked summary) and writes, for the screen's two poses: the filtered 24+40
candidate and filtered dense64 (S,T) of the stratified + central-crop sky rays, the 5
witness rays x 7 geometry depths, and the 5 witness rays at the seven forward shifts.
`cases.txt` routes the fixture; `reference.npz` is read by the checker only.
"""
from __future__ import annotations
import argparse, importlib.util, json, math
from pathlib import Path
import numpy as np

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('fog_density_runtime_screen', HERE / 'fog_density_runtime_screen.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
SHIFTS = (-5500., -5000., -4500., 0., 4500., 5000., 5500.)


def basis(pose):
    forward = np.asarray(pose['forward'], np.float64); up = np.asarray(pose['up'], np.float64)
    right = np.cross(up, forward); right /= np.linalg.norm(right); up = np.cross(forward, right)
    return right, up, forward


def both(origin, directions, limits, chroma, store):
    ev = m.filtered_eval(store)
    cand = m.integrate(origin, directions, limits, chroma, ev, 'candidate', 0)['full']
    dense = m.integrate(origin, directions, limits, chroma, ev, 'dense', 64.)['full']
    return {'candidate_S': cand['S'], 'candidate_T': cand['T'], 'dense64_S': dense['S'], 'dense64_T': dense['T']}


def run(asset_data, output):
    if output.exists():
        raise ValueError(f'output directory already exists: {output}')
    output.mkdir(parents=True)
    manifest = json.loads((asset_data / 'manifest.json').read_text())
    profile = next(x for x in manifest['profiles'] if x['name'] == 'foggreenoutlands')
    if abs(float(profile['base_sigma']) * 1.5 - m.SIGMA) > 1e-15:
        raise ValueError('sigma')
    volume = m.fog.decode_packet(asset_data / 'foggreenoutlands.fogbin', manifest)
    chroma = (volume[..., :3].sum((0, 1, 2), dtype=np.float64) / volume[..., 3].sum(dtype=np.float64)).astype(m.F)
    store = m.LazyStore(); arrays = {}; pops = m.populations()
    lines = ['sigma %r' % float(m.SIGMA), 'chroma %r %r %r' % tuple(float(np.float32(c)) for c in chroma)]

    def case(name, origin, pose, mode, value):
        r, u, f = basis(pose)
        lines.append(' '.join([name] + [repr(float(v)) for v in (*origin, *r, *u, *f)] + [mode, value]))

    for pose in m.screen.pose_rows():
        name = pose['name']; origin = np.asarray(pose['origin'], np.float64); allr = m.rays(pose)
        combined = np.unique(np.concatenate([y * m.W + x for x, y in pops.values()]))
        sx, sy = pops['stratified']; wi = np.array([j * 32 + i for i, j in m.WITNESS]); wpix = sy[wi] * m.W + sx[wi]
        case(f'{name}_sky', origin, pose, 'sky', '0')
        arrays[f'{name}_sky_pixels'] = combined
        for k, v in both(origin, allr[combined], np.full(len(combined), m.FAR, m.F), chroma, store).items():
            arrays[f'{name}_sky_{k}'] = v
        t = math.tan(math.radians(30))
        vl = np.linalg.norm(np.stack(((2 * (sx[wi] + .5) / m.W - 1) * (m.W / m.H) * t, (1 - 2 * (sy[wi] + .5) / m.H) * t, np.ones(5)), 1), axis=1)
        arrays[f'{name}_witness_pixels'] = wpix
        for index, depth in enumerate(m.DEPTHS):
            limits = np.minimum((depth / vl) * vl, m.FAR).astype(m.F)
            case(f'{name}_depth{index}', origin, pose, 'depth', repr(float(depth)))
            for k, v in both(origin, allr[wpix], limits, chroma, store).items():
                arrays[f'{name}_depth{index}_{k}'] = v
        for index, shift in enumerate(SHIFTS):
            if shift == 0.:
                continue
            moved = origin + shift * np.asarray(pose['forward'], np.float64)
            case(f'{name}_shift{index}', moved, pose, 'sky', '0')
            for k, v in both(moved, allr[wpix], np.full(5, m.FAR, m.F), chroma, store).items():
                arrays[f'{name}_shift{index}_{k}'] = v
        for index, kind in enumerate(('zero', 'nan', 'inf')):
            case(f'{name}_invalid{index}', origin, pose, 'invalid', kind)
        case(f'{name}_empty', origin, pose, 'empty', '0')
    (output / 'cases.txt').write_text('\n'.join(lines) + '\n')
    np.savez(output / 'reference.npz', **arrays)
    record = dict(schema=1, width=m.W, height=m.H, shifts=SHIFTS, depths=m.DEPTHS.tolist(),
                  screen_sha256=m.digest(HERE / 'fog_density_runtime_screen.py'), exporter_sha256=m.digest(__file__),
                  cases_sha256=m.digest(output / 'cases.txt'), reference_sha256=m.digest(output / 'reference.npz'),
                  manifest_sha256=m.digest(asset_data / 'manifest.json'), packet_sha256=m.digest(asset_data / 'foggreenoutlands.fogbin'))
    (output / 'reference.json').write_text(json.dumps(record, indent=2) + '\n')
    return record


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--asset-data', type=Path, required=True); p.add_argument('--output', type=Path, required=True)
    a = p.parse_args(); print(json.dumps(run(a.asset_data, a.output)))


if __name__ == '__main__':
    main()
