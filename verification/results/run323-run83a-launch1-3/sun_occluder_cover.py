#!/usr/bin/env python3
"""Fraction of depth_1_<frame> pixels (channel 0 > -1, i.e. geometry) in square boxes of half-size 5/20/60 px around the sun pixel.
usage: sun_occluder_cover.py RUNDIR frame:px:py ..."""
import sys, numpy as np
D = sys.argv[1]; W, H = 5120, 1440
for a in sys.argv[2:]:
    fr, px, py = map(int, a.split(':'))
    d = np.fromfile(f'{D}/depth_1_{fr}.rgba32f', dtype=np.float32).reshape(H, W, 4)[..., 0]
    out = []
    for h in (5, 20, 60):
        b = d[max(0,py-h):py+h+1, max(0,px-h):px+h+1]; out.append(f'h{h}={float((b > -1).mean()):.3f}')
    print(fr, px, py, ' '.join(out))
