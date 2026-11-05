#!/usr/bin/env python3
"""Sky step cost and the star-body check for star textures without a colour token (sky_textures.py).

  PYTHONPATH=verification/probe:verification/analysis python3 verification/results/sky-litcube/measure.py \
      [--real DIR] [--out result.json]

Synthetic: twenty 2048x2048 DXT1 loose dds/nebula_litcube<N>_stars_diff.dds (black, 600 stars, full mips) in a temp
game; times mark_loose cold (built) and warm (reused). --real DIR (read-only, e.g. /mnt/c/X3/dds): classify every
.dds/.pck, the star-body dark share and bake_stars time of each name match, and the slot collisions of the families.
Nothing is written under DIR. Prints and writes one compact JSON."""
import argparse
import json
import sys
import tempfile
import time
import unittest.mock
from collections import Counter
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import lod_atlas  # noqa: E402
import sky_textures as st  # noqa: E402


def synthetic(n=2048, stars=600, seed=0):
    rng = np.random.default_rng(seed)
    img = np.zeros((n, n, 4), np.uint8)
    img[..., 3] = 255
    ys, xs = rng.integers(2, n - 4, stars), rng.integers(2, n - 4, stars)
    for y, x in zip(ys, xs):
        img[y:y + 2, x:x + 2, :3] = 230
    return lod_atlas.write_dds(lod_atlas.mip_chain(img.astype(np.float64)), 'DXT1')


def measure_synthetic(count=20):
    data = synthetic()
    with tempfile.TemporaryDirectory() as folder:
        game = Path(folder)
        (game / 'dds').mkdir()
        entries = {}
        for i in range(count):
            rel = f'dds/nebula_litcube{i}_stars_diff.dds'
            (game / rel).write_bytes(data)
            entries[rel] = [dict(path=rel, source='loose:' + rel, loose=game / rel)]
        assets = unittest.mock.Mock(entries=entries)
        assets.candidates = lambda base: entries.get(base, [])
        t0 = time.perf_counter()
        cold = st.mark_loose(game, assets)
        t1 = time.perf_counter()
        warm = st.mark_loose(game, assets)
        t2 = time.perf_counter()
    return dict(count=count, bytes_each=len(data), built=sum(r['status'] == 'built' for r in cold['rows']),
                reused=sum(r['status'] == 'reused' for r in warm['rows']), cold_s=round(t1 - t0, 3),
                warm_s=round(t2 - t1, 3), cold_per_texture_ms=round(1000 * (t1 - t0) / count, 1),
                warm_per_texture_ms=round(1000 * (t2 - t1) / count, 1))


def measure_real(folder):
    rows, fams = [], Counter()
    for f in sorted(Path(folder).iterdir()):
        c = st.classify('dds/' + f.name)
        if c is None:
            continue
        data = f.read_bytes()
        t0 = time.perf_counter()
        rgba = lod_atlas.decode_dds(data).astype(np.float32)
        lum = 0.299 * rgba[..., 0] + 0.587 * rgba[..., 1] + 0.114 * rgba[..., 2]
        dark = float((lum < st.STAR_BODY_DARK_LUMA).mean())
        try:
            _, info = st.bake_stars(data, *c)
        except st.NotStarBody:
            info = None
        rows.append(dict(name=f.name, family=c[0], colour=c[1], dark=round(dark, 4), marked=info is not None,
                         lane=info and info['lane'], k=info and info['k'], ms=round(1000 * (time.perf_counter() - t0))))
        fams[st.fnv1a32(c[0].encode()) & 255] += 1
    dark = [r['dark'] for r in rows]
    return dict(matched=len(rows), marked=sum(r['marked'] for r in rows),
                not_star_body=[r['name'] for r in rows if not r['marked']],
                no_colour=sum(r['colour'] is None for r in rows), dark_min=min(dark, default=None),
                dark_median=float(np.median(dark)) if dark else None,
                families=len({r['family'] for r in rows}), slots_used=len(fams),
                slots_shared=sum(1 for v in fams.values() if v > 1),
                ms_median=float(np.median([r['ms'] for r in rows])) if rows else None,
                k_min=min((r['k'] for r in rows if r['marked']), default=None),
                k_max=max((r['k'] for r in rows if r['marked']), default=None),
                coloured=sorted({(r['colour'], r['lane']) for r in rows if r['colour']}, key=str))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--real')
    ap.add_argument('--out')
    a = ap.parse_args()
    res = dict(synthetic=measure_synthetic())
    if a.real:
        res['real'] = measure_real(a.real)
    text = json.dumps(res, indent=1, default=str)
    print(text)
    if a.out:
        Path(a.out).write_text(text + '\n')


if __name__ == '__main__':
    main()
