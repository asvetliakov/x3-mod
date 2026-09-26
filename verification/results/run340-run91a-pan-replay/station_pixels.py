#!/usr/bin/env python3
"""Run 91 A pan replay, step 1: routed pixels by view-depth band on a capture frame, motion magnitude, thin-region share
(region hold h > 0 in the taa_age fraction: frac(|age|) * 65536 = h + 128 * codeC, resolve.hlsl holdCode) and far weight
share (farw > 0 from the depth, far ramp 60 / 68 units/px at 5120 px, p00 0.5 -> view depth 76,800 / 87,040 units).
Also prints the bounding box of the dominant connected component of each band (coarse 16-px grid).
Usage: station_pixels.py <dir> <frame> [x0 y0 x1 y1]"""
import sys, numpy as np
W, H = 5120, 1440
P22, P32 = 1.00000298, -6.00001812
d, fr = sys.argv[1], int(sys.argv[2])
box = list(map(int, sys.argv[3:7])) if len(sys.argv) >= 7 else None
m = np.memmap(f'{d}/motion_1_{fr}.rgba32f', np.float32, 'r', shape=(H, W, 4))
z = np.array(np.memmap(f'{d}/depth_1_{fr}.rgba32f', np.float32, 'r', shape=(H, W, 4))[:, :, 0])
age = np.abs(np.fromfile(f'{d}/taa_age_1_{fr}.r32f', np.float32).reshape(H, W))
held = np.where(age <= 65, np.modf(age)[0] * 65536, 0)
h = np.round(held) % 128  # region hold count 0..L
thin = h > 0.5
ys, xs = np.mgrid[0:H, 0:W]
dx = m[:, :, 0] * W - (xs + 0.5); dy = m[:, :, 1] * H - (ys + 0.5); mag = np.hypot(dx, dy)
valid = m[:, :, 3] == 1
routed = z >= 0
with np.errstate(divide='ignore', invalid='ignore'):
    vz = np.where(routed, P32 / (z - P22), np.inf)
farw = routed & (vz > 76800)
sel0 = np.ones((H, W), bool)
if box:
    x0, y0, x1, y1 = box; sel0[:] = False; sel0[y0:y1, x0:x1] = True
print(f'frame {fr}: routed {routed.mean():.4f} thin(all px) {thin.mean():.4f} age p50 {np.median(np.floor(age[routed])):.0f}')
for lo, hi, name in [(0, 2000, 'z<2k'), (2000, 10000, '2k-10k'), (10000, 25000, '10k-25k (2-5 km)'), (25000, 76800, '25k-76.8k'), (76800, 1e30, '>76.8k far')]:
    s = sel0 & routed & (vz >= lo) & (vz < hi)
    n = int(s.sum())
    if not n: print(f'  {name:18s} px 0'); continue
    v = s & valid
    yy, xx = np.nonzero(s)
    print(f'  {name:18s} px {n:8d} |d| p50 {np.median(mag[v]):6.2f} p90 {np.percentile(mag[v], 90):6.2f} thin {thin[s].mean():.3f} farw>0 {farw[s].mean():.3f} '
          f'bbox x {np.percentile(xx, 1):.0f}-{np.percentile(xx, 99):.0f} y {np.percentile(yy, 1):.0f}-{np.percentile(yy, 99):.0f}')
