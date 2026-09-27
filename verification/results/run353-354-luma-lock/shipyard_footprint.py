#!/usr/bin/env python3
"""Footprint of the shipyard (argon_spacedock, box from object_bounds, bounds_names_out.txt) in the depth lane:
pixels in the box with view depth in [zlo, zhi]; F = 2 z / (p00 W) = z / 960 at 1920 px, p00 1.00
(src/temporal/resolve.h:162-164); share at or above far_f0 60 / far_f1 68; median RT1 displacement.
Usage: shipyard_footprint.py <dir> <frame> x0 y0 x1 y1 zlo zhi"""
import sys, numpy as np
W, H = 1920, 1080; P22, P32 = 1.00000298, -6.00001812
d, fr = sys.argv[1], int(sys.argv[2]); x0, y0, x1, y1 = map(int, sys.argv[3:7]); zlo, zhi = map(float, sys.argv[7:9])
z = np.array(np.memmap(f'{d}/depth_1_{fr}.rgba32f', np.float32, 'r', shape=(H, W, 4))[y0:y1, x0:x1, 0])
mo = np.array(np.memmap(f'{d}/motion_1_{fr}.rgba32f', np.float32, 'r', shape=(H, W, 4))[y0:y1, x0:x1])
ys, xs = np.mgrid[y0:y1, x0:x1]
with np.errstate(divide='ignore', invalid='ignore'): vz = np.where(z >= 0, P32 / (z - P22), 0)
m = (vz >= zlo) & (vz <= zhi); F = vz[m] / 960.
disp = np.hypot(mo[..., 0] * W - 0.5 - xs, mo[..., 1] * H - 0.5 - ys)[m]
print(f'{d[-6:]} frame {fr} box {x0},{y0},{x1},{y1} px {m.sum()} z p5/p50/p95 {np.percentile(vz[m],[5,50,95]).round(0)} '
      f'F p5/p50/p95 {np.percentile(F,[5,50,95]).round(1)} share F>=60 {100*(F>=60).mean():.1f} % F>=68 {100*(F>=68).mean():.1f} % |d| p50 {np.median(disp):.2f} px')
