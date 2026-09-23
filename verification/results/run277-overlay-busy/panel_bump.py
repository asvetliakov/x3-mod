#!/usr/bin/env python3
"""Run 74 A (run277): bump (tangent-space normal, x in alpha, y in R) and texel density of one
source material on its own map vs the merged atlas, same faces. Prints texels per unit 3D area
(source vs atlas, ratio), and per mip level the area-weighted mean decoded (x, y), the mean
|xy| and the length of the mean vector over each face's 7 samples (coherence).
Usage: panel_bump.py BODY MAT"""
import sys, math
from pathlib import Path
import numpy as np
sys.path.insert(0, '/Users/asvetl/x3-mod/tools/analysis')
import bob1, lod_overlay, lod_atlas, body_materials
name, want = sys.argv[1], int(sys.argv[2])
game = Path(bob1.DEFAULT_GAME)
oa, _ = lod_overlay.original_assets(game); ia = lod_overlay.Assets(game)
orig = bob1.parse(oa.read_entry(bob1.resolve_body(oa, name))); inst = bob1.parse(ia.read_entry(bob1.resolve_body(ia, name)))
om, im = bob1.materials(orig), bob1.materials(inst)
L0 = bob1.lods(orig)[0]; R1 = bob1.lods(inst)[1]
def key(P, f): return tuple(sorted(tuple(P[i][1:4]) for i in f[:3]))
src = {key(L0['points'], f): f for p in L0['parts'] for g in p['groups'] if g['material'] == want for f in g['faces']}
pairs = [(src[key(R1['points'], f)], f, g['material']) for p in R1['parts'] for g in p['groups'] for f in g['faces'] if key(R1['points'], f) in src]
def uvarea(P, f, w, h):
    (a, b), (c, d), (e, g_) = [lod_atlas.point_uv(P[i]) for i in f[:3]]
    return 0.5 * abs((c - a) * (g_ - b) - (e - a) * (d - b)) * w * h
sb = lod_atlas.decode_dds(lod_atlas.texture_bytes(oa, body_materials.slots(om[want])['bump']))
dm = pairs[0][2]
db_bytes = lod_atlas.texture_bytes(ia, body_materials.slots(im[dm])['bump'])
A3 = sum(body_materials.face_area(L0['points'], s) for s, _, _ in pairs)
ts = sum(uvarea(L0['points'], s, *sb.shape[1::-1]) for s, _, _ in pairs)
w0, h0, mips, _ = lod_atlas.dds_format(db_bytes)
ta = sum(uvarea(R1['points'], d, w0, h0) for _, d, _ in pairs)
print(f'mat{want}: faces {len(pairs)} (merged group mat{dm}); source bump {sb.shape[1]}x{sb.shape[0]}, atlas {w0}x{h0}')
print(f'texels per 3D area: source {ts / A3:.3e}, atlas {ta / A3:.3e}, atlas/source linear ratio {math.sqrt(ta / ts):.4f}')
B = np.array([(1/3, 1/3, 1/3), (.6, .2, .2), (.2, .6, .2), (.2, .2, .6), (.8, .1, .1), (.1, .8, .1), (.1, .1, .8)])
sbytes = lod_atlas.texture_bytes(oa, body_materials.slots(om[want])['bump']); smips = lod_atlas.dds_format(sbytes)[2]
def stats(data, level, P, which, wrap):
    img = lod_atlas.decode_dds(data, level).astype(np.float64)
    h, w = img.shape[:2]; acc = np.zeros(4); W = 0
    for s, d, _ in pairs:
        f = s if which == 'src' else d; Q = L0['points'] if which == 'src' else R1['points']
        uv = B @ np.array([lod_atlas.point_uv(Q[i]) for i in f[:3]])
        x = np.floor(uv[:, 0] * w).astype(int); y = np.floor(uv[:, 1] * h).astype(int)
        x, y = (x % w, y % h) if wrap else (np.clip(x, 0, w - 1), np.clip(y, 0, h - 1))
        t = img[y, x]; nx = t[:, 3] / 127.5 - 1; ny = t[:, 0] / 127.5 - 1
        a = body_materials.face_area(L0['points'], s)
        acc += a * np.array([nx.mean(), ny.mean(), np.hypot(nx, ny).mean(), math.hypot(nx.mean(), ny.mean())]); W += a
    return acc / W
for lv in range(0, 6):
    row = []
    for who, data, n, wrap in (('src', sbytes, smips, True), ('atlas', db_bytes, mips, False)):
        if lv < n:
            m = stats(data, lv, None, who, wrap)
            row.append(f'{who} L{lv} mean x={m[0]:+.3f} y={m[1]:+.3f} |xy|={m[2]:.3f} |mean|={m[3]:.3f}')
    print(' | '.join(row))
