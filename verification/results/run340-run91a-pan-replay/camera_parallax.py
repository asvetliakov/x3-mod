#!/usr/bin/env python3
"""Run 91 A pan replay: split a capture frame's RT1 motion into the rotation-only camera path (the resolve's cameraUV, the
far-plane reprojection of camera_state R / P of frames f and f-1) and the camera-relative remainder the motion weight cap
reads (resolve.hlsl 699: parallax2 = min(|previousUV - cameraUV|^2, |screen|^2); cap 0.7 at >= 8 px, 1 at <= 2 px).
Tries both matrix conventions and reports per box the median |motion|, |rotation path| and |relative| and the cap.
Usage: camera_parallax.py <dir> <frame> x0 y0 x1 y1 zlo zhi"""
import re, subprocess, sys, os, numpy as np
W, H = 5120, 1440
P22, P32 = 1.00000298, -6.00001812
d, fr = sys.argv[1], int(sys.argv[2]); x0, y0, x1, y1 = map(int, sys.argv[3:7]); zlo, zhi = map(float, sys.argv[7:9])
log = [p for p in os.listdir(d) if p.startswith('session-')][0]
rows = subprocess.run(['grep', '-E', r'^camera_state device=1 frame=(%d|%d) ' % (fr, fr - 1), f'{d}/{log}'], capture_output=True, text=True).stdout.splitlines()
cam = {}
for l in rows:
    kv = dict(re.findall(r'(\w+)=([-\w.+]+)', l)); f = int(kv['frame'])
    cam[f] = dict(P=[float(kv[k]) for k in ('p00', 'p11', 'p20', 'p21')], R=np.array([[float(kv['r%d%d' % (i, j)]) for j in range(3)] for i in range(3)]))
ys, xs = np.mgrid[y0:y1, x0:x1]
m = np.array(np.memmap(f'{d}/motion_1_{fr}.rgba32f', np.float32, 'r', shape=(H, W, 4))[y0:y1, x0:x1])
z = np.array(np.memmap(f'{d}/depth_1_{fr}.rgba32f', np.float32, 'r', shape=(H, W, 4))[y0:y1, x0:x1, 0])
with np.errstate(divide='ignore', invalid='ignore'):
    vz = np.where(z >= 0, P32 / (z - P22), 0)
sel = (vz >= zlo) & (vz <= zhi) & (m[..., 3] == 1)
mx = m[..., 0] * W - 0.5; my = m[..., 1] * H - 0.5  # previous pixel position (raster), jitter not removed
nx = 2 * xs / W - 1; ny = 1 - 2 * ys / H
p00, p11, p20, p21 = cam[fr]['P']
dv = np.stack([(nx - p20) / p00, (ny - p21) / p11, np.ones_like(nx, float)], -1)
for conv in (0, 1):
    Rc, Rp = cam[fr]['R'], cam[fr - 1]['R']
    world = dv @ Rc if conv == 0 else dv @ Rc.T
    pv = world @ Rp.T if conv == 0 else world @ Rp
    q00, q11, q20, q21 = cam[fr - 1]['P']
    pnx = pv[..., 0] / pv[..., 2] * q00 + q20; pny = pv[..., 1] / pv[..., 2] * q11 + q21
    rx = (pnx + 1) / 2 * W; ry = (1 - pny) / 2 * H
    rot = np.hypot(rx - xs, ry - ys)[sel]; rel = np.hypot(mx - rx, my - ry)[sel]; scr = np.hypot(mx - xs, my - ys)[sel]
    par = np.minimum(rel, scr)
    cap = np.clip(np.maximum(0.7, 1 - 0.3 * (par ** 2 - 4) / (64 - 4)), 0, 1)
    print(f'frame {fr} conv {conv} px {int(sel.sum())}: |motion| p50 {np.median(scr):.2f} |rotation path| p50 {np.median(rot):.2f} '
          f'|relative| p50 {np.median(rel):.2f} p90 {np.percentile(rel, 90):.2f}  cap p50 {np.median(cap):.3f} p10 {np.percentile(cap, 10):.3f}')
