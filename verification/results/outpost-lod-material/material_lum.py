#!/usr/bin/env python3
"""Mean HDR luminance per source material in frame 2410, from the record-0 projection (project_record.py
OUT.npz, draws 22,23), masks eroded by R px (the projection fits the logged bounds to 25.6 px summed error,
so masks are approximate). Also the screen-row span of mat 5 (support beams). Usage: material_lum.py NPZ CAPTURE_DIR [R]"""
import sys
import numpy as np
d = np.load(sys.argv[1]); cap = sys.argv[2]; R = int(sys.argv[3]) if len(sys.argv) > 3 else 2
x0, y0 = int(d['x0']), int(d['y0']); ids, fm = d['id_src'], d['fmat']
H, W = ids.shape
hdr = np.memmap(f'{cap}/hdr_1_2410.rgba16f', dtype=np.float16, mode='r').reshape(1440, 5120, 4)[y0:y0 + H, x0:x0 + W, :3].astype(np.float32)
lum = hdr @ np.array([0.2126, 0.7152, 0.0722], np.float32)
mat = np.where(ids >= 0, fm[np.maximum(ids, 0)], -1)
ring = np.zeros_like(mat, bool); ring[607 - y0:668 - y0, 2505 - x0:2626 - x0] = True
for m in sorted(set(mat[mat >= 0].tolist())):
    k = mat == m
    e = k.copy()
    for dy in range(-R, R + 1):
        for dx in range(-R, R + 1):
            e &= np.roll(np.roll(k, dy, 0), dx, 1)
    if e.sum() < 20: continue
    er = e & ring
    ys = np.nonzero(e.any(1))[0] + y0
    print(f'mat {m:2d}: eroded px {e.sum():5d} mean lum {lum[e].mean():.3f} median {np.median(lum[e]):.3f}'
          + (f' | in ring {er.sum():5d} mean {lum[er].mean():.3f}' if er.sum() >= 20 else '') + f' rows {ys.min()}-{ys.max()}')
