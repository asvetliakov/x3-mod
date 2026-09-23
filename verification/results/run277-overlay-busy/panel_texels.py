#!/usr/bin/env python3
"""Run 74 A (run277): sample source textures (original LOD 0 UVs) against the installed merged
record's textures (record 1 UVs) on the same faces, per source material. 7 barycentric samples
per face, area-weighted, mip 0, nearest texel, source wrapped. Prints mean RGBA per slot
(diffuse, light, specular) source vs merged, and for alpha materials the share of samples whose
alpha passes the logged alpha test (ref 1, GREATEREQUAL) with the own alpha map vs the merged
group's (mat11) alpha map. Usage: panel_texels.py BODY MAT [MAT...]"""
import sys, collections
from pathlib import Path
import numpy as np
sys.path.insert(0, '/Users/asvetl/x3-mod/tools/analysis')
import bob1, lod_overlay, lod_atlas, body_materials
name = sys.argv[1]; want = [int(v) for v in sys.argv[2:]]
game = Path(bob1.DEFAULT_GAME)
oa, _ = lod_overlay.original_assets(game)
ia = lod_overlay.Assets(game)
orig = bob1.parse(oa.read_entry(bob1.resolve_body(oa, name)))
inst = bob1.parse(ia.read_entry(bob1.resolve_body(ia, name)))
om, im = bob1.materials(orig), bob1.materials(inst)
L0 = bob1.lods(orig)[0]; R1 = bob1.lods(inst)[1]
cache = {}
def tex(assets, mats, mi, slot):
    nm = body_materials.slots(mats[mi]).get(slot)
    key = (id(assets), nm)
    if key not in cache:
        try:
            src = lod_atlas.texture_source(assets, nm)
            cache[key] = None if src is None else (lod_atlas.decode_dds(src[0]) if src[1] == 'dds' else lod_atlas.decode_image(src[0], nm)).astype(np.float64)
        except Exception as e:
            print('texture', nm, 'error', e); cache[key] = None
    return cache[key], nm
B = np.array([(1/3, 1/3, 1/3), (.6, .2, .2), (.2, .6, .2), (.2, .2, .6), (.8, .1, .1), (.1, .8, .1), (.1, .1, .8)])
def samples(P, face):
    uv = np.array([lod_atlas.point_uv(P[i]) for i in face[:3]])
    return B @ uv
def fetch(img, uvs, wrap):
    h, w = img.shape[:2]
    x = uvs[:, 0] * w; y = uvs[:, 1] * h
    if wrap: xi = np.floor(x).astype(int) % w; yi = np.floor(y).astype(int) % h
    else: xi = np.clip(np.floor(x).astype(int), 0, w - 1); yi = np.clip(np.floor(y).astype(int), 0, h - 1)
    return img[yi, xi]
def key(P, f): return tuple(sorted(tuple(P[i][1:4]) for i in f[:3]))
src_of = {}
for part in L0['parts']:
    for g in part['groups']:
        for f in g['faces']:
            src_of[key(L0['points'], f)] = (g['material'], f)
acc = collections.defaultdict(lambda: collections.defaultdict(lambda: [np.zeros(4), 0.0]))
alpha_pass = collections.defaultdict(lambda: [0.0, 0.0, 0.0])
for part in R1['parts']:
    for g in part['groups']:
        for f in g['faces']:
            hit = src_of.get(key(R1['points'], f))
            if not hit or hit[0] not in want: continue
            m0, f0 = hit
            a = body_materials.face_area(L0['points'], f0)
            if a <= 0: continue
            su = samples(L0['points'], f0); du = samples(R1['points'], f)
            for slot in ('diffuse', 'light', 'specular', 'alpha'):
                s_img, _ = tex(oa, om, m0, slot); d_img, _ = tex(ia, im, g['material'], slot)
                if s_img is not None:
                    r = acc[m0][('src', slot)]; r[0] += a * fetch(s_img, su, True).mean(0); r[1] += a
                if d_img is not None:
                    r = acc[m0][('merged', slot)]; r[0] += a * fetch(d_img, du, g['material'] < 49).mean(0); r[1] += a
            sa, _ = tex(oa, om, m0, 'alpha'); da, _ = tex(ia, im, g['material'], 'alpha')
            if sa is not None and da is not None:
                ap = alpha_pass[m0]
                ap[0] += a * (fetch(sa, su, True)[:, 3 if sa[..., 3].std() > 0 else 0] >= 1).mean()
                ap[1] += a * (fetch(da, du, True)[:, 3 if da[..., 3].std() > 0 else 0] >= 1).mean(); ap[2] += a
for m in want:
    print(f'mat{m} textures: ' + ' '.join(f'{s}={tex(oa, om, m, s)[1]}' for s in ('diffuse', 'light', 'specular', 'alpha')))
    for slot in ('diffuse', 'light', 'specular'):
        row = []
        for who in ('src', 'merged'):
            v, w = acc[m][(who, slot)]
            row.append(f'{who} ' + (','.join(f'{x:.1f}' for x in v / w) if w else 'none'))
        print(f'  {slot:8s} mean RGBA (0..255, area-weighted): ' + ' | '.join(row))
    if alpha_pass[m][2]:
        s, d, w = alpha_pass[m]
        print(f'  alpha-test pass share: own alpha map {s / w:.3f} vs merged group alpha map {d / w:.3f}')
