#!/usr/bin/env python3
"""Depth lane (depth_1_N .r z/w, .b clip w) under the bolt pixels of bolt_band2.py (hue excess > T): per frame the
count of bolt pixels over any geometry, the lane w percentiles there, and how many have lane z/w below the bolt's
smallest possible z/w for the frame (z/w of the nearest bolt vertex = 1.000003 - 6/w_min, w_min from bullet_camera.py:
geometry that would be nearer than every bolt).  usage: lane_under_bolts.py g|b T WMIN FRAME [FRAME...]"""
import sys
import numpy as np
W, H = 5120, 1440
ch = {'g': 1, 'b': 2}[sys.argv[1]]; T = float(sys.argv[2]); wmin = float(sys.argv[3]); oth = [c for c in range(3) if c != ch]
zmin = 1.000003 - 6.0 / wmin
for f in sys.argv[4:]:
    h = np.fromfile(f'hdr_1_{f}.rgba16f', np.float16).reshape(H, W, 4).astype(np.float32)[..., :3]
    d = np.fromfile(f'depth_1_{f}.rgba32f', np.float32).reshape(H, W, 4)
    bolt = (h[..., ch] - np.maximum(h[..., oth[0]], h[..., oth[1]])) > T
    geo = (d[..., 0] >= 0) & (d[..., 0] < 1); m = bolt & geo
    w = d[..., 2][m]; z = d[..., 0][m]
    far = w > 20000
    print(f, f'bolt_over_geo={int(m.sum())} far(w>20000)={int(far.sum())} near={int((~far).sum())}',
          'far w p1/p50/p99', [round(float(v)) for v in np.percentile(w[far], [1, 50, 99])] if far.any() else None,
          'far z/w min', round(float(z[far].min()), 7) if far.any() else None,
          'near w p1/p50/p99', [round(float(v)) for v in np.percentile(w[~far], [1, 50, 99])] if (~far).any() else None,
          f'bolt z/w min {zmin:.6f}', f'geo_nearer_than_all_bolts={int((z < zmin).sum())}')
