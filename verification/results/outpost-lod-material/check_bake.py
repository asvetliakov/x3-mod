#!/usr/bin/env python3
"""Read-only check of a scratch bake of stations/sadiablo/freighter_base (lod_overlay.py --batch --only ... --out
OUT, never under the game): per source material of record 0, the C group / merged material its faces land in
(faces matched by their three point positions), that material's g_CullMode and g_MatDiffuseStrength, its
diffuse binding, and for atlased materials the area-weighted mean Rec.709 luma of the diffuse atlas at the C
face centroids against the source diffuse texture at the source face centroids (mip 0, bilinear), with the
ratio (atlas mean x merged strength) / (source mean x own strength). Prints the C group list (draws) too.
Usage (repo root): python3 check_bake.py GAME OUT [MATERIAL ...]"""
import gzip
import sys
sys.path.insert(0, 'tools/analysis')
from pathlib import Path
import numpy as np
import bob1, body_materials, lod_atlas, lod_overlay

game, out = Path(sys.argv[1]), Path(sys.argv[2])
show = {int(x) for x in sys.argv[3:]}
name = 'stations/sadiablo/freighter_base'
assets, _ = body_materials.original_assets(game)
src_tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, name)), lod_overlay.MAX_TRAILING)
src_mats, r0 = bob1.materials(src_tree), bob1.lods(src_tree)[0]
cat = out / 'addon' / '13.cat'
ents = {e['path']: e for e in lod_overlay.read_catalogue(cat)}


def member(path):
    e = ents[path]
    with cat.with_suffix('.dat').open('rb') as f:
        f.seek(e['offset'])
        return f.read(e['size']).translate(lod_overlay.XOR33)


body = next(p for p in ents if p.endswith('freighter_base.pbb'))
raw = member(body)
tree = bob1.parse(gzip.decompress(raw) if raw[:2] == b'\x1f\x8b' else raw, lod_overlay.MAX_TRAILING)
mats, ladder = bob1.materials(tree), bob1.lods(tree)
C = ladder[1]
word = lambda m, n: lod_atlas.param_word(m, n)
fx = lambda w: None if w is None else round(w / 65536, 4)
diff_name = lambda m: lod_atlas.material_slots(m)['diffuse']
print('C groups (draws):', [(pi, g['material'], len(g['faces'])) for pi, p in enumerate(C['parts'])
                            for g in p['groups']])
for i in sorted({g['material'] for p in C['parts'] for g in p['groups']}):
    print(f'  material {i}: cull {word(mats[i], b"g_CullMode")} diffuse {fx(word(mats[i], b"g_MatDiffuseStrength"))}'
          f' specular {fx(word(mats[i], b"g_MatSpecularStrength"))} t_DiffuseTexture {diff_name(mats[i])}')
pos = lambda pts, f: tuple(sorted(tuple(pts[i][1:4]) for i in f[:3]))
src_of = {}
for p in r0['parts']:
    for g in p['groups']:
        for f in g['faces']:
            src_of.setdefault(pos(r0['points'], f), []).append((g['material'], f))
atlas = None
textures = lod_atlas.Textures(assets)
luma = lambda img: img[..., :3].astype(np.float64) @ lod_atlas.LUMA_709
acc = {}
for p in C['parts']:
    for g in p['groups']:
        for f in g['faces']:
            hit = src_of.get(pos(C['points'], f))
            if not hit:
                continue
            m, sf = hit[0]
            a = acc.setdefault(m, dict(classes=set(), faces=0, area=0.0, at=0.0, sr=0.0))
            a['classes'].add(g['material'])
            a['faces'] += 1
            if g['material'] != m:                           # atlased: sample both at the centroid
                if atlas is None:
                    dname = diff_name(mats[g['material']]).decode('latin1').split('\\')[-1].rsplit('.', 1)[0]
                    atlas = lod_atlas.decode_dds(gzip.decompress(member(f'dds/{dname}.pck'))).astype(np.float32)
                src = textures.get(diff_name(src_mats[m]))
                if src is None:
                    continue
                w = body_materials.face_area(r0['points'], sf)
                cu = np.array([np.mean([lod_atlas.point_uv(C['points'][i])[k] for i in f[:3]]) for k in (0, 1)])
                su = np.array([np.mean([lod_atlas.point_uv(r0['points'][i])[k] for i in sf[:3]]) for k in (0, 1)])
                at = lod_atlas.bilinear(atlas, cu[:1], cu[1:], False)[0]
                sr = lod_atlas.bilinear(src.astype(np.float32), su[:1] % 1.0, su[1:] % 1.0, True)[0]
                a['area'] += w
                a['at'] += w * luma(at)
                a['sr'] += w * luma(sr)
for m in sorted(acc):
    if show and m not in show:
        continue
    a = acc[m]
    (c,) = a['classes'] if len(a['classes']) == 1 else (tuple(sorted(a['classes'])),)
    cm = mats[c] if isinstance(c, int) else None
    s_i, s_c = word(src_mats[m], b'g_MatDiffuseStrength'), cm and word(cm, b'g_MatDiffuseStrength')
    row = (f'src {m:2d}: faces {a["faces"]:5d} -> C material {c} cull {word(src_mats[m], b"g_CullMode")}->'
           f'{cm and word(cm, b"g_CullMode")} diffuse {fx(s_i)}->{fx(s_c)}')
    if a['area'] > 0:
        at, sr = a['at'] / a['area'], a['sr'] / a['area']
        row += f'; luma atlas {at:.2f} src {sr:.2f}; (atlas x merged) / (src x own) {at * s_c / (sr * s_i):.4f}'
    else:
        row += f'; own textures: t_DiffuseTexture {diff_name(cm) if cm else "?"} (source {diff_name(src_mats[m])})'
    print(row)
