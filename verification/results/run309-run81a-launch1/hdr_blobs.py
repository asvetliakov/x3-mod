#!/usr/bin/env python3
"""Brightest 64x64-px tiles (by max luminance) of an HDR capture, with their NDC. usage: hdr_blobs.py FILE [W H]"""
import sys, numpy as np
p = sys.argv[1]; W, H = 5120, 1440
a = np.fromfile(p, dtype=np.float16).reshape(H, W, 4).astype(np.float32)
lum = 0.2126*a[..., 0] + 0.7152*a[..., 1] + 0.0722*a[..., 2]
T = 64; t = lum[:H//T*T, :W//T*T].reshape(H//T, T, W//T, T)
mx = np.nanmax(t, axis=(1, 3)); mean = np.nanmean(t, axis=(1, 3))
idx = np.argsort(mx, axis=None)[::-1][:12]
for i in idx:
    ty, tx = divmod(int(i), mx.shape[1]); cx = tx*T+T/2; cy = ty*T+T/2
    print(f'tile ({tx*T},{ty*T}) max {mx[ty,tx]:.2f} mean {mean[ty,tx]:.3f} ndc=({cx/W*2-1:.3f},{1-cy/H*2:.3f})')
print('frame mean lum', float(np.nanmean(lum)))
