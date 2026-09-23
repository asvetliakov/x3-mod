#!/usr/bin/env python3
"""Run 74 A (run277): light-map value seen by one source material's faces, per mip level:
source light map (own UVs, wrapped) vs merged light atlas (record 1 UVs, clamped). Nearest
fetch at 7 barycentric samples per face, area-weighted mean RGBA, plus the mean over a
(2r+1)^2 texel box at that level (r=1,2: a widened/anisotropic footprint proxy) and the share of
samples whose box holds a texel with luma > 32/255 (a neighbour's emitter inside the footprint).
Usage: panel_light_mips.py BODY MAT [SLOT: light (default), diffuse, specular]"""
import sys
from pathlib import Path
import numpy as np
sys.path.insert(0, '/Users/asvetl/x3-mod/tools/analysis')
import bob1, lod_overlay, lod_atlas, body_materials
name, want = sys.argv[1], int(sys.argv[2]); SLOT = sys.argv[3] if len(sys.argv) > 3 else 'light'
game = Path(bob1.DEFAULT_GAME)
oa, _ = lod_overlay.original_assets(game); ia = lod_overlay.Assets(game)
orig = bob1.parse(oa.read_entry(bob1.resolve_body(oa, name))); inst = bob1.parse(ia.read_entry(bob1.resolve_body(ia, name)))
om, im = bob1.materials(orig), bob1.materials(inst)
L0 = bob1.lods(orig)[0]; R1 = bob1.lods(inst)[1]
def key(P, f): return tuple(sorted(tuple(P[i][1:4]) for i in f[:3]))
src = {key(L0['points'], f): f for p in L0['parts'] for g in p['groups'] if g['material'] == want for f in g['faces']}
pairs = [(src[key(R1['points'], f)], f, g['material']) for p in R1['parts'] for g in p['groups'] for f in g['faces'] if key(R1['points'], f) in src]
B = np.array([(1/3, 1/3, 1/3), (.6, .2, .2), (.2, .6, .2), (.2, .2, .6), (.8, .1, .1), (.1, .8, .1), (.1, .1, .8)])
sdat = lod_atlas.texture_bytes(oa, body_materials.slots(om[want])[SLOT])
ddat = lod_atlas.texture_bytes(ia, body_materials.slots(im[pairs[0][2]])[SLOT])
print(f'mat{want}: {len(pairs)} faces; source light {lod_atlas.dds_format(sdat)[:3]}, atlas light {lod_atlas.dds_format(ddat)[:3]}')
def run(data, level, which, wrap):
    img = lod_atlas.decode_dds(data, level).astype(np.float64); h, w = img.shape[:2]
    lum = img[..., :3] @ np.array([.2126, .7152, .0722])
    acc = np.zeros(4); box = {1: np.zeros(4), 2: np.zeros(4)}; hot = {1: 0.0, 2: 0.0}; W = 0
    for s, d, _ in pairs:
        f, Q = (s, L0['points']) if which == 'src' else (d, R1['points'])
        uv = B @ np.array([lod_atlas.point_uv(Q[i]) for i in f[:3]])
        x = np.floor(uv[:, 0] * w).astype(int); y = np.floor(uv[:, 1] * h).astype(int)
        a = body_materials.face_area(L0['points'], s); W += a
        fx = (lambda v: v % w) if wrap else (lambda v: np.clip(v, 0, w - 1)); fy = (lambda v: v % h) if wrap else (lambda v: np.clip(v, 0, h - 1))
        acc += a * img[fy(y), fx(x)].mean(0)
        for r in (1, 2):
            ox, oy = np.meshgrid(np.arange(-r, r + 1), np.arange(-r, r + 1))
            X = fx(x[:, None] + ox.ravel()[None]); Y = fy(y[:, None] + oy.ravel()[None])
            box[r] += a * img[Y, X].reshape(-1, 4).mean(0); hot[r] += a * (lum[Y, X].max(1) > 32).mean()
    return acc / W, {r: box[r] / W for r in box}, {r: hot[r] / W for r in hot}
for lv in range(0, 7):
    parts = []
    for who, data, wrap in (('src', sdat, True), ('atlas', ddat, False)):
        if lv < lod_atlas.dds_format(data)[2]:
            m, bx, ht = run(data, lv, who, wrap)
            parts.append(f'{who} L{lv} rgb={m[0]:.1f},{m[1]:.1f},{m[2]:.1f} a={m[3]:.1f} box1 rgb={bx[1][0]:.1f},{bx[1][1]:.1f},{bx[1][2]:.1f} box2 rgb={bx[2][0]:.1f},{bx[2][1]:.1f},{bx[2][2]:.1f} hot1={ht[1]:.3f} hot2={ht[2]:.3f}')
    print(' | '.join(parts))
