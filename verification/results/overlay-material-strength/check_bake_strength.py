#!/usr/bin/env python3
"""Read-only check of a scratch bake (lod_overlay.py --batch --only ... --out OUT, never under the game) for the
option-B strength encoding (docs/architecture/overlay-material-strength.md 7): per body in the overlay catalogue,
the C groups (draws) and merged materials with their g_CullMode / g_MatDiffuseStrength, the marker's strength rows
(s_merged, mean, max, per member s_i and alpha byte), the header mark of every atlas DDS (read_dds_mark), the
specular atlas format, and the decoded specular alpha at each member's tile (mean over the tile content at mip 0)
against 255 * s_i / s_merged / 2, with the recovered ratio 2 a s_merged / s_i (expect 1.000 +- 0.004) and the
background alpha. Only the scratch catalogue is read (no game textures). Usage (repo root):
python3 check_bake_strength.py OUT [SLOT]"""
import gzip
import json
import sys
sys.path.insert(0, 'tools/analysis')
from pathlib import Path
import numpy as np
import bob1, lod_atlas, lod_overlay

out = Path(sys.argv[1])
slot = int(sys.argv[2]) if len(sys.argv) > 2 else 13
cat = out / 'addon' / f'{slot:02d}.cat'
marker = json.load(open(out / 'addon' / f'{slot:02d}.x3m-lod.json'))
ents = {e['path']: e for e in lod_overlay.read_catalogue(cat)}


def member(path):
    e = ents[path]
    with cat.with_suffix('.dat').open('rb') as f:
        f.seek(e['offset'])
        return f.read(e['size']).translate(lod_overlay.XOR33)


word = lambda m, n: lod_atlas.param_word(m, n)
fx = lambda w: None if w is None else round(w / 65536, 4)
for b in marker['bodies']:
    print(f"== {b['name']}: draws {b.get('draws')}")
    body = next(p for p in ents if p.endswith(b['name'].split('/')[-1] + '.pbb'))
    raw = member(body)
    tree = bob1.parse(gzip.decompress(raw) if raw[:2] == b'\x1f\x8b' else raw, lod_overlay.MAX_TRAILING)
    mats, ladder = bob1.materials(tree), bob1.lods(tree)
    C = ladder[b['new_lod']]
    print('C groups (part, material, faces):', [(pi, g['material'], len(g['faces'])) for pi, p in enumerate(C['parts'])
                                                for g in p['groups']])
    for i in sorted({g['material'] for p in C['parts'] for g in p['groups']}):
        print(f'  material {i}: cull {word(mats[i], b"g_CullMode")} diffuse {fx(word(mats[i], b"g_MatDiffuseStrength"))}'
              f' t_SpecularTexture {lod_atlas.material_slots(mats[i], ("specular",))["specular"]}')
    atlas = b.get('atlas') or {}
    for t in atlas.get('textures', []):
        dds = gzip.decompress(member(t['member']))
        mark = lod_atlas.read_dds_mark(dds)
        print(f"  atlas {t['slot']}: {t['format']} {t['dds_bytes']} B mark {mark} (marker says {t.get('mark')})")
    spec = next((t for t in atlas.get('textures', []) if t['slot'] == 'specular'), None)
    if not spec:
        print('  no specular atlas')
        continue
    img = lod_atlas.decode_dds(gzip.decompress(member(spec['member']))).astype(np.float64)
    tiles = atlas['tiles']
    used = np.zeros(img.shape[:2], bool)
    for t in tiles:
        (cx, cy), (cw, ch) = t['origin'], t['content']
        used[cy - atlas['gutter']:cy + ch + atlas['gutter'], cx - atlas['gutter']:cx + cw + atlas['gutter']] = True
    bg = img[..., 3][~used]
    print(f'  background alpha: mean {bg.mean():.2f} min {bg.min():.0f} max {bg.max():.0f} over {bg.size} texels'
          if bg.size else '  no background')
    worst = 0.0
    for row in atlas.get('strength', []):
        merged, mean, smax = row['merged'], row['mean'], row['max']
        print(f"  class material {row['material']}: s_merged {fx(merged)} (mean {fx(mean)}, max {fx(smax)},"
              f" rule {'mean' if abs(mean - merged) < 1 else 'max/2'})")
        for m, (s_i, a) in row['members'].items():
            m = int(m)
            tile = next((t for t in tiles if m in t['mats']), None)
            if tile is None:
                print(f'    member {m}: no tile'); continue
            (cx, cy), (cw, ch) = tile['origin'], tile['content']
            got = img[cy:cy + ch, cx:cx + cw, 3]
            expect = lod_atlas.strength_alpha(s_i, merged) if s_i is not None else lod_atlas.STRENGTH_BACKGROUND
            ratio = 2 * got.mean() / 255 * merged / s_i if s_i else float('nan')
            err = float(np.abs(got - a).max())
            worst = max(worst, err)
            print(f'    member {m:2d}: s_i {fx(s_i)} alpha byte {a} (expect {expect}) decoded mean {got.mean():.2f}'
                  f' min {got.min():.0f} max {got.max():.0f} tile {cw}x{ch}; 2 a s_merged / s_i = {ratio:.4f}')
    print(f'  worst |decoded - byte| over tile contents: {worst:.0f}/255')
