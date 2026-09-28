#!/usr/bin/env python3
"""Camera rotation deg/frame and translation u/frame, and dt_ms, every 10th frame over (a, b]. Usage: settle.py dir a b"""
import glob, re, sys, numpy as np
d, a, b = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]); cam = {}; dt = {}
for l in open(glob.glob(f'{d}/session-*.log')[0], errors='replace'):
    if l.startswith('camera_state device=1 frame='):
        f = int(re.search(r'frame=(\d+)', l).group(1))
        if a - 1 <= f <= b: cam[f] = (np.array(list(map(float, re.search(r' t=([-\d.]+),([-\d.]+),([-\d.]+)', l).groups()))), float(re.search(r' rotation_deg=([-\d.]+)', l).group(1)))
    elif l.startswith('frame_end device=1 '):
        f = int(re.search(r'frame=(\d+)', l).group(1))
        if a < f <= b: dt[f] = float(re.search(r'dt_ms=([\d.]+)', l).group(1))
el = 0
for f in range(a + 1, b + 1):
    el += dt.get(f, 0)
    if (f - a) % 10 == 0 or f == b: print(f'frame {f} +{el:.0f} ms rot {cam[f][1]:.3f} move {np.linalg.norm(cam[f][0] - cam[f-1][0]):.0f} dt {dt.get(f)}')
