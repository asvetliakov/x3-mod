#!/usr/bin/env python3
"""Read-only: luminance statistics of the outpost's middle ring in the two F8 HDR captures (Run 202).
Distant ring box: hdr_1_2410 x 2505..2625, y 607..667 (user-given). Close box: the same box mapped through the
logged object_bounds of the station (frame 2410 draws 22-23 x 2479-2639 y 548-716 -> frame 5735 draws 18-40
x 2155-2567 y 398-797). Background (alpha/coverage) is excluded by depth < 1.
Usage: python3 ring_stats.py CAPTURE_DIR"""
import sys
import numpy as np
d = sys.argv[1]; W, H = 5120, 1440
def box_map(x, y):
    return (2155 + (x - 2479) * (2567 - 2155) / (2639 - 2479), 398 + (y - 548) * (797 - 398) / (716 - 548))
boxes = {'2410': (2505, 2625, 607, 667)}
a = box_map(2505, 607); b = box_map(2625, 667)
boxes['5735'] = (int(a[0]), int(b[0]), int(a[1]), int(b[1]))
for f, (x0, x1, y0, y1) in boxes.items():
    h = np.memmap(f'{d}/hdr_1_{f}.rgba16f', dtype=np.float16, mode='r').reshape(H, W, 4)[y0:y1, x0:x1, :3].astype(np.float32)
    z = np.memmap(f'{d}/depth_1_{f}.rgba32f', dtype=np.float32, mode='r').reshape(H, W, 4)[y0:y1, x0:x1, 0]
    L = (h @ np.array([0.2126, 0.7152, 0.0722], np.float32))[z < 1]
    med = np.median(L)
    p = np.percentile(L, [5, 25, 50, 75, 95])
    print(f'{f} box x{x0}-{x1} y{y0}-{y1} px {L.size} mean {L.mean():.3f} std {L.std():.3f} cv {L.std() / L.mean():.2f} '
          f'p5/25/50/75/95 {np.round(p, 3).tolist()} p95/p5 {p[4] / max(p[0], 1e-4):.1f} dark(<0.4 med) {np.mean(L < 0.4 * med):.3f} '
          f'mean rgb {np.round(h.reshape(-1, 3)[z.reshape(-1) < 1].mean(0), 3).tolist()}')
