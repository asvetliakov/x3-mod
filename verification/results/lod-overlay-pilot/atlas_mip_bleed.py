#!/usr/bin/env python3
"""Light-atlas mip levels 0-4 against the tiles' own light maps resampled to the same density.

For every atlas body in an overlay root (addon/NN.x3m-lod.json + NN.cat/.dat; read only)
the light atlas DDS is decoded at mip L. The reference atlas at level L is built
independently of lod_atlas's mip code: black (a NULL light map) where no tile is, and in
every tile's footprint (content + gutter, rounded out to level-L texels) its source light
map area-resampled from the repeating source over each level-L texel (atlas texel x covers
level-0 units [x*2^L, (x+1)*2^L), which map to source periods lo + (x - origin) * span /
content); a texel overlapping a tile's content takes that tile's value. Texels covered by
two footprints are ambiguous and left out. Exhaust tiles hold a glow material
(lod_overlay.glow_materials, the engine glows). Per level, errors are the per-texel mean
|RGB| (0..255) and excess is the Rec.601 luma of atlas - reference:
  exhaust content      texels wholly inside an exhaust tile's content;
  exhaust edge+gutter  the rest of its footprint (what bilinear reads at the content edge);
  outside band         the BAND-texel ring outside the exhaust footprints (the neighbours'
                       gutters and content, or unused area), where exhaust light must not go;
  other tiles          texels overlapping the content of non-exhaust tiles.
The max figures include the DXT error (level 0 shows it alone); a bleed shows as a positive
mean excess and as edge+gutter error growing over level 0.
Level 0 carries only the encoding (DXT5) error, so growth over level 0 is the mip error.

  python3 verification/results/lod-overlay-pilot/atlas_mip_bleed.py <overlay root> [...]
"""
import json
import math
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import bob1  # noqa: E402
import lod_atlas  # noqa: E402
import lod_overlay  # noqa: E402
from inspect_x3 import read_catalogue  # noqa: E402
from sector_fog_census import unpack  # noqa: E402

LUMA = np.array([0.299, 0.587, 0.114])
BAND = 2


def weights(k0, k1, scale, origin, content, lo, span, src_n):
    m = np.zeros((k1 - k0, src_n), np.float64)
    step = scale * span / content * src_n
    for r, k in enumerate(range(k0, k1)):
        a = (lo + (k * scale - origin) * span / content) * src_n
        j = math.floor(a)
        while j < a + step:
            cov = min(a + step, j + 1) - max(a, j)
            if cov > 0:
                m[r, j % src_n] += cov
            j += 1
        m[r] /= step
    return m


def reference(t, src, level, g):
    s = 1 << level
    (cx, cy), (cw, ch) = t['origin'], t['content']
    x0, x1 = math.floor((cx - g) / s), math.ceil((cx + cw + g) / s)
    y0, y1 = math.floor((cy - g) / s), math.ceil((cy + ch + g) / s)
    if src is None:
        return (x0, y0), np.broadcast_to(np.array(lod_atlas.NULL_TEXEL), (y1 - y0, x1 - x0, 4))
    H, W = src.shape[:2]
    my = weights(y0, y1, s, cy, ch, t['lo'][1], t['span'][1], H)
    mx = weights(x0, x1, s, cx, cw, t['lo'][0], t['span'][0], W)
    block = np.tensordot(np.tensordot(my, src.astype(np.float64), axes=(1, 0)), mx, axes=(1, 1)).transpose(0, 2, 1)
    return (x0, y0), block


def stats(v):
    v = np.asarray(v, np.float64)
    return (float(v.mean()), float(np.percentile(v, 95))) if v.size else (0.0, 0.0)


def footprint(t, level, g):
    s = 1 << level
    (cx, cy), (cw, ch) = t['origin'], t['content']
    return ((math.floor((cx - g) / s), math.ceil((cx + cw + g) / s), math.floor((cy - g) / s),
             math.ceil((cy + ch + g) / s)),
            (math.floor(cx / s), math.ceil((cx + cw) / s), math.floor(cy / s), math.ceil((cy + ch) / s)),
            (math.ceil(cx / s), math.floor((cx + cw) / s), math.ceil(cy / s), math.floor((cy + ch) / s)))


def paint(mask, box, nl, value=True):
    x0, x1, y0, y1 = box
    mask[max(0, y0):min(nl, y1), max(0, x0):min(nl, x1)] = value


def body_report(root, body, entries, raw, assets, textures):
    at = body['atlas']
    tex = next(t for t in at['textures'] if t['slot'] == 'light')
    e = entries[tex['member']]
    dds = unpack(bytes(v ^ 0x33 for v in raw[e['offset']:e['offset'] + e['size']]))
    src_tree = bob1.parse(assets.get(body['member'])[0])
    mats = bob1.materials(src_tree)
    tile_mats = sorted({m for t in at['tiles'] for m in t['mats']})
    glow = lod_overlay.glow_materials(assets, mats, tile_mats)
    g, n = at['gutter'], at['size']
    exhaust = [i for i, t in enumerate(at['tiles']) if set(t['mats']) & glow]
    last = len(body['source_thresholds']) - 1
    sr = body.get('source_record', last)
    print(f'{body["name"]}: atlas {n}x{n} gutter {g} source_record {"last" if sr == last else sr} tiles'
          f' {len(at["tiles"])} exhaust tiles {exhaust} (glow materials {sorted(glow)})')
    for level in range(0, 5):
        img = lod_atlas.decode_dds(dds, level).astype(np.float64)[:, :, :3]
        nl = img.shape[0]
        ref = np.zeros((nl, nl, 3))                       # unused area: black (NULL light map)
        cover = np.zeros((nl, nl), np.int32)              # footprints covering a texel
        owners = [np.zeros((nl, nl), bool) for _ in range(4)]   # exhaust inside / exhaust edge+gutter / other / ring
        blocks = []
        for i, t in enumerate(at['tiles']):
            name = t['names'].get('light')
            src = textures.get(None if name is None else name.encode('latin1'))
            (x0, y0), block = reference(t, src, level, g)
            h, w = block.shape[:2]
            fp, over, inside = footprint(t, level, g)
            blocks.append(((x0, y0), block[:, :, :3], over))
            sub = np.zeros((nl, nl), bool)
            paint(sub, fp, nl)
            cover += sub
            ys, xs = slice(max(0, y0), min(nl, y0 + h)), slice(max(0, x0), min(nl, x0 + w))
            ref[ys, xs] = block[ys.start - y0:ys.stop - y0, xs.start - x0:xs.stop - x0, :3]
            if i in exhaust:
                paint(owners[0], inside, nl)
                edge = sub.copy()
                paint(edge, inside, nl, False)
                owners[1] |= edge
                ring = np.zeros((nl, nl), bool)
                paint(ring, (fp[0] - BAND, fp[1] + BAND, fp[2] - BAND, fp[3] + BAND), nl)
                owners[3] |= ring
            else:
                paint(owners[2], over, nl)
        for (x0, y0), block, over in blocks:              # content wins over a neighbour's gutter
            h, w = block.shape[:2]
            ox0, ox1, oy0, oy1 = over
            ys, xs = slice(max(0, oy0), min(nl, oy1)), slice(max(0, ox0), min(nl, ox1))
            ref[ys, xs] = block[ys.start - y0:ys.stop - y0, xs.start - x0:xs.stop - x0]
        ex_fp = np.zeros((nl, nl), bool)
        for i in exhaust:
            paint(ex_fp, footprint(at['tiles'][i], level, g)[0], nl)
        ambiguous = cover > 1
        owners[3] &= ~ex_fp
        err = np.abs(img - ref).mean(-1)
        excess = ((img - ref) * LUMA).sum(-1)
        cls = [o & ~ambiguous for o in owners]
        (em, ep), (gm, gp), (om, op) = (stats(err[c]) for c in cls[:3])
        ring = cls[3]
        print(f'  mip {level} ({nl}x{nl}): exhaust content err mean/p95 {em:.2f}/{ep:.2f} ({cls[0].sum()} texels);'
              f' exhaust edge+gutter err mean/p95/max {gm:.2f}/{gp:.2f}/{err[cls[1]].max(initial=0):.1f}'
              f' ({cls[1].sum()}); outside band max RGB {img[ring].max(initial=0):.0f} luma excess max/mean'
              f' {excess[ring].max(initial=0):.1f}/{excess[ring].mean() if ring.any() else 0:.2f} ({ring.sum()});'
              f' other tiles err mean/p95 {om:.2f}/{op:.2f} luma excess max/mean {excess[cls[2]].max(initial=0):.1f}/'
              f'{excess[cls[2]].mean() if cls[2].any() else 0:.3f}'
              f' ({cls[2].sum()}); ambiguous texels {ambiguous.sum()}')


def main(roots):
    assets, skipped = lod_overlay.original_assets(bob1.DEFAULT_GAME)
    textures = lod_atlas.Textures(assets)
    for root in map(Path, roots):
        (mk,) = sorted(root.glob('addon/[0-9][0-9]' + lod_overlay.MARKER_SUFFIX))
        cat = mk.with_name(mk.name[:2] + '.cat')
        marker = json.loads(mk.read_text())
        raw = cat.with_suffix('.dat').read_bytes()
        entries = {e['path']: e for e in read_catalogue(cat)}
        print(f'overlay {cat.name} ({"installed" if root.resolve() == bob1.DEFAULT_GAME.resolve() else "build"}),'
              f' dat {len(raw)} bytes; sources read without {skipped}')
        for body in marker['bodies']:
            if body.get('atlas'):
                body_report(root, body, entries, raw, assets, textures)


if __name__ == '__main__':
    main(sys.argv[1:])
