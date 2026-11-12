#!/usr/bin/env python3
"""Per-pixel nearest-texel occlusion and selected cascade for a capture frame (no PCF, no bias),
to compare with the hdr frame. usage: occlusion_image.py FRAME OUT_PREFIX [x0 y0 x1 y1 step]
Writes OUT_PREFIX_occl.png (white lit, black occluded, blue sky) and OUT_PREFIX_casc.png
(cascade colour: c0 red, c1 green, c2 blue, c3 yellow, c4 magenta) and prints counts."""
import sys, numpy as np
from PIL import Image
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from project import rows, vec, CAP, W, H

f, pre = sys.argv[1], sys.argv[2]
x0, y0, x1, y1, st = (map(int, sys.argv[3:8]) if len(sys.argv) > 7 else (0, 0, W, H, 4))
cam, cas = rows(f)
R = np.array([[float(cam[f'r{i}{j}']) for j in range(3)] for i in range(3)])
t = vec(cam['t']); p00 = float(cam['p00']); p11 = float(cam['p11'])
d = np.memmap(CAP + f'depth_1_{f}.rgba32f', dtype='<f4', mode='r', shape=(H, W, 4))
ys, xs = np.mgrid[y0:y1:st, x0:x1:st]
z = np.array(d[y0:y1:st, x0:x1:st, 2], dtype=np.float64); ok = z > 0
nx = (xs + .5) / W * 2 - 1; ny = 1 - (ys + .5) / H * 2
pw = (np.stack([nx * z / p00, ny * z / p11, z], -1) - t) @ R.T
chosen = np.full(z.shape, -1); occl = np.zeros(z.shape, bool); gap = np.full(z.shape, np.nan)
for c in sorted(cas):
    k = cas[c]
    if k.get('valid') != '1': continue
    r, u, fw, ce = vec(k['right']), vec(k['up']), vec(k['forward']), vec(k['center'])
    E = float(k['extent']); L = float(k['depth_light']); B = float(k['depth_behind']); N = int(k['size'])
    q = pw - ce; sx = q @ r / E; sy = q @ u / E; sz = (q @ fw + L) / (L + B)
    first = ok & (np.maximum(abs(sx), abs(sy)) <= 1) & (sz >= 0) & (sz <= 1) & (chosen < 0)
    if not first.any(): continue
    chosen[first] = c
    col = ((sx * .5 + .5) * N).astype(int).clip(0, N - 1); row = ((-sy * .5 + .5) * N).astype(int).clip(0, N - 1)
    m = np.memmap(CAP + f'shadow_map{c}_1_{f}.r32f', dtype='<f4', mode='r', shape=(N, N))
    md = np.array(m)[row[first], col[first]].astype(np.float64)
    occl[first] = md < sz[first] - 1e-6
    gap[first] = (sz[first] - md) * (L + B)  # metres between receiver and stored caster along the sun axis
img = np.zeros(z.shape + (3,), np.uint8); img[ok & ~occl] = 255; img[~ok] = (0, 0, 90)
Image.fromarray(img).save(pre + '_occl.png')
pal = {0: (255, 0, 0), 1: (0, 200, 0), 2: (0, 80, 255), 3: (230, 230, 0), 4: (230, 0, 230)}
ci = np.zeros(z.shape + (3,), np.uint8)
for c, col in pal.items(): ci[chosen == c] = col
Image.fromarray(ci).save(pre + '_casc.png')
print('frame', f, 'pixels', int(ok.sum()), ' '.join(f'c{c}={int((chosen == c).sum())}/occl={int((occl & (chosen == c)).sum())}' for c in range(5)))
