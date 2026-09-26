#!/usr/bin/env python3
"""Run 91 A pan blur: per capture frame, the RT1 motion magnitude by view-depth band.

Usage: capture_motion_bands.py <capture dir> <frame> [<frame> ...]
Reads motion_1_<f>.rgba32f (RG = previous UV incl. half texel, A = 1 valid / -1 sentinel) and
depth_1_<f>.rgba32f (lane 0 = device depth, -1 unrouted) at 5120x1440 via memmap; prints one row per
band: pixel share, valid-motion share, median / p90 displacement in px/frame. Jitter (<= 0.5 px per
axis) is not removed, so displacements below ~0.7 px are noise-dominated.
View depth z = P32 / (d - P22) with the logged p22 = 1.00000298, p32 = -6.00001812 (camera_state rows).
Footprint (units/px) = 2 z / (p00 W) = z / 1280 at p00 0.5, W 5120.
"""
import sys, numpy as np
W, H = 5120, 1440
P22, P32 = 1.00000298, -6.00001812
BANDS = [(0, 2000, 'z<2k (own ship/near)'), (2000, 10000, '2k-10k (0.4-2 km)'),
         (10000, 25000, '10k-25k (2-5 km)'), (25000, 76800, '25k-76.8k (5-15 km)'),
         (76800, 87040, 'far ramp 60-68 u/px'), (87040, 1e30, 'far >= 68 u/px (W 0.985)')]
d = sys.argv[1]
for fr in sys.argv[2:]:
    m = np.memmap(f'{d}/motion_1_{fr}.rgba32f', np.float32, 'r', shape=(H, W, 4))
    z = np.memmap(f'{d}/depth_1_{fr}.rgba32f', np.float32, 'r', shape=(H, W, 4))[:, :, 0]
    ys, xs = np.mgrid[0:H, 0:W]
    valid = m[:, :, 3] == 1
    dx = m[:, :, 0] * W - (xs + 0.5)
    dy = m[:, :, 1] * H - (ys + 0.5)
    mag = np.hypot(dx, dy)
    routed = z >= 0
    with np.errstate(divide='ignore', invalid='ignore'):
        vz = np.where(routed, P32 / (z - P22), np.inf)
    print(f'frame {fr}: routed-depth px {routed.mean():.4f}, valid motion px {valid.mean():.4f}')
    for lo, hi, name in BANDS:
        sel = routed & (vz >= lo) & (vz < hi)
        n = int(sel.sum())
        if n == 0:
            print(f'  {name:28s} px 0'); continue
        v = sel & valid
        mv = mag[v]
        mdx = dx[v]
        if mv.size:
            print(f'  {name:28s} px {n:8d} valid {v.sum()/n:.3f} |d| p50 {np.median(mv):6.2f} p90 {np.percentile(mv,90):6.2f} dx p50 {np.median(mdx):6.2f}')
        else:
            print(f'  {name:28s} px {n:8d} valid 0')
    unr = ~routed
    print(f'  unrouted/sky px {int(unr.sum())} valid motion among them {(unr & valid).sum()}')
