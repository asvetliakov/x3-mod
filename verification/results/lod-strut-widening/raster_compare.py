#!/usr/bin/env python3
"""Orthographic raster of C before (--no-widen bake) and after (widened bake), down the three body axes, 8 Halton
jitter phases of one sample per pixel at the 5120x1440 focal length (1,280 px): k = 1280 s / (r_raw 640), at the
sizes given plus s_d and T_pad (lod-strut-widening.md section 5). Faces draw in C's order with depth (opaque
first, then the widened group source-over with z-write; tools/analysis/lod_raster.py); colour = Rec.709 luminance
of the diffuse atlas at the face's UV centroid for atlased and widened faces, 0.5 for the alpha group (its own
textures, not decoded; alpha test not modelled, the same in both bakes); widened faces take the diffuse atlas alpha
at the centroid.

Per axis view and size: light over sky (after / before, the phase-mean summed image), light over a hull colour
(the area-weighted mean face colour of the unwidened C, standing in for the station's own hull behind the struts:
1 + (sum after - sum before) / sum before over sky), the flip share over sky (pixels covered in some phases and not
in others, over the pixels ever covered) and the flicker over sky (sum over pixels of the 8-phase std over the sum
of the 8-phase mean). With @L the body's record 0 (drawn above T_pad, identical in both bakes) is rastered once at
size L on a 1024 x 1024 crop round its centre (flip share and flicker; flat colour 0.5). Reads the scratch outputs'
addon catalogues; baked bodies stay local.

--flat: every face colour 1 and the hull colour 0.5 (geometry and coverage alpha only; the two bakes' atlases differ in scale, so the atlas
colours at the face centroids differ slightly between them).

Usage: raster_compare.py [--flat] FLAT_OUT WIDE_OUT NAME=S[,S...][@L] [...]"""
import gzip
import json
import sys
import time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
import bob1                          # noqa: E402
import lod_atlas                     # noqa: E402
import lod_raster                    # noqa: E402
from inspect_x3 import read_catalogue  # noqa: E402

FOCAL = 1280.0
LUMA = np.array([0.2126, 0.7152, 0.0722])
CROP = 1024


def members(out):
    """{member path: bytes (gzip removed)} of every addon catalogue under a bake output."""
    got = {}
    for cat in sorted((Path(out) / 'addon').glob('*.cat')):
        raw = np.fromfile(cat.with_suffix('.dat'), np.uint8) ^ 0x33
        for e in read_catalogue(cat):
            data = raw[e['offset']:e['offset'] + e['size']].tobytes()
            got[e['path']] = gzip.decompress(data) if data[:2] == b'\x1f\x8b' else data
    return got


def body(out, name, flat=False):
    man = {}
    for p in (Path(out) / 'addon').glob('*.x3m-lod.json'):
        man.update({b['name']: b for b in json.loads(p.read_text())['bodies']})
    m = man[name]
    mem = members(out)
    tree = bob1.parse(mem[m['member']])
    mats, ladder = bob1.materials(tree), bob1.lods(tree)
    tex = {t['slot']: t['member'] for t in m['atlas']['textures']}
    atlas = lod_atlas.decode_dds(mem[tex['diffuse']]).astype(np.float64)
    f = lod_raster.record_faces(ladder[m['new_lod']], mats)
    atlased = set(m['atlas']['materials']) | set(m['atlas'].get('widened_materials', []))
    blended = np.isin(f['M'], m['atlas'].get('widened_materials', []))
    on = np.isin(f['M'], list(atlased))
    colour = np.full(len(f['M']), 0.5)
    alpha = np.ones(len(f['M']))
    if on.any():
        with np.errstate(all='ignore'):
            s = lod_atlas.bilinear(atlas, f['uv'][on, 0], f['uv'][on, 1], False)
            colour[on] = s[:, :3] @ LUMA / 255
        alpha[on] = np.where(blended[on], s[:, 3] / 255, 1.0)
    if flat:
        colour[:] = 1.0
    P = f['P']
    area = np.linalg.norm(np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0]), axis=1) / 2
    r_raw = max(np.linalg.norm(p[1:4]) for p in ladder[0]['points'] if p[0] & 1)
    return dict(faces=f, colour=colour, alpha=alpha, blended=blended, r_raw=r_raw, t_pad=m['pad_threshold'],
                divisor=(m.get('widen') or {}).get('divisor', 2.0),
                hull=0.5 if flat else float((colour * area).sum() / area.sum()),
                lod0=lod_raster.record_faces(ladder[0], mats))


def phases(b, axis, k, origin, size, bg=0.0):
    tri, d, front = lod_raster.view(b['faces'], axis)
    imgs, covs = [], []
    for jit in lod_raster.JITTER8:
        r = lod_raster.render(tri[front], d[front], b['colour'][front], b['alpha'][front], b['blended'][front], k,
                              origin, size, jitter=jit, background=bg)
        imgs.append(r['image']); covs.append(r['cover'] > 0)
    return np.stack(imgs), np.stack(covs)


def flips(imgs, covs):
    ever, always = covs.any(0), covs.all(0)
    mean = imgs.mean(0)
    return (float((ever & ~always).sum() / max(ever.sum(), 1)), float(imgs.std(0).sum() / max(mean.sum(), 1e-9)),
            int(ever.sum()))


def main():
    args = sys.argv[1:]
    flat = '--flat' in args
    args = [a for a in args if a != '--flat']
    flat_out, wide_out = args[0], args[1]
    for spec in args[2:]:
        name, sizes = spec.split('=')
        sizes, _, lod0 = sizes.partition('@')
        t0 = time.time()
        before, after = body(flat_out, name, flat), body(wide_out, name, flat)
        hull = before['hull']
        labelled = [(f's {x}', float(x)) for x in sizes.split(',')] + [
            ('s_d', after['t_pad'] / after['divisor']), ('T_pad', float(after['t_pad']))]
        print(f'== {name}: r_raw {before["r_raw"]:.0f}, faces {len(before["colour"])} -> {len(after["colour"])},'
              f' widened faces {int(after["blended"].sum())}, mean widened alpha'
              f' {after["alpha"][after["blended"]].mean() if after["blended"].any() else 0:.3f}; hull colour {hull:.3f}')
        for label, s in labelled:
            k = FOCAL * s / (before['r_raw'] * 640)
            print(f'  {label} = {s:g} (k {k:.6f} px/unit, radius {k * before["r_raw"]:.0f} px):')
            for axis in range(3):
                tri0 = lod_raster.view(before['faces'], axis)[0]
                tri1 = lod_raster.view(after['faces'], axis)[0]
                origin, size = lod_raster.frame(np.concatenate([tri0, tri1]), k)
                ib, cb = phases(before, axis, k, origin, size)
                ia, ca = phases(after, axis, k, origin, size)
                hb = phases(before, axis, k, origin, size, hull)[0]
                ha = phases(after, axis, k, origin, size, hull)[0]
                light = ib.mean(0).sum()
                fb, fa = flips(ib, cb), flips(ia, ca)
                print(f'    axis {axis}: light sky {ia.mean(0).sum() / light:.4f}, hull'
                      f' {1 + (ha.mean(0).sum() - hb.mean(0).sum()) / light:.4f}; flip {fb[0]:.3f} -> {fa[0]:.3f};'
                      f' flicker {fb[1]:.4f} -> {fa[1]:.4f}; covered px {fb[2]} -> {fa[2]}')
        if lod0:
            s = float(lod0)
            k = FOCAL * s / (before['r_raw'] * 640)
            f = before['lod0']
            n = len(f['M'])
            print(f'  LOD 0 at s {s:g} (k {k:.6f}, radius {k * before["r_raw"]:.0f} px, {CROP} px crop round the'
                  f' centre; identical in both bakes):')
            for axis in range(3):
                tri, d, front = lod_raster.view(f, axis)
                origin = -CROP / 2 / k * np.ones(2)
                imgs, covs = [], []
                for jit in lod_raster.JITTER8:
                    r = lod_raster.render(tri[front], d[front], np.full(front.sum(), 0.5), np.ones(front.sum()),
                                          np.zeros(front.sum(), bool), k, origin, (CROP, CROP), jitter=jit)
                    imgs.append(r['image']); covs.append(r['cover'] > 0)
                fl = flips(np.stack(imgs), np.stack(covs))
                print(f'    axis {axis}: flip {fl[0]:.3f}, flicker {fl[1]:.4f}, covered px {fl[2]} (faces {n})')
        print(f'  ({time.time() - t0:.0f} s)')


if __name__ == '__main__':
    main()
