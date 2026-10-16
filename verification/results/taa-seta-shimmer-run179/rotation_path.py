#!/usr/bin/env python3
"""Rotation-only camera path from camera_state rows: which orientation reproduces the RT1 motion of pixels beyond
100,000 view units (where translation parallax is small)? Usage: rotation_path.py <capdir> <log> <frame>"""
import sys, re, subprocess, numpy as np
W, H = 5120, 1440
cap, log, fr = sys.argv[1], sys.argv[2], int(sys.argv[3])
def row(tag, f):
    l = subprocess.run(['grep', '-m1', '-E', f'^{tag} device=1 frame={f} ', log], capture_output=True, text=True).stdout
    return dict(re.findall(r'(\w+)=(\S+)', l))
def cam(f):
    kv = row('camera_state', f)
    return np.array([[float(kv[f'r{i}{j}']) for j in range(3)] for i in range(3)]), float(kv['p00']), float(kv['p11'])
R1, p00, p11 = cam(fr); R0 = cam(fr - 1)[0]
j = row('motion_output_frame', fr); jx, jy = float(j['jitter_x']), float(j['jitter_y'])
d = np.memmap(f'{cap}/depth_1_{fr}.rgba32f', np.float32, 'r', shape=(H, W, 4))[::2, ::2]
m = np.memmap(f'{cap}/motion_1_{fr}.rgba32f', np.float32, 'r', shape=(H, W, 4))[::2, ::2]
ys, xs = np.mgrid[0:H:2, 0:W:2]
ok = (m[..., 3] == 1) & (d[..., 2] > 1e5) & (d[..., 0] < 1)
px, py = xs[ok] + 0.5, ys[ok] + 0.5
sx = m[..., 0][ok] * W - px + jx; sy = m[..., 1][ok] * H - py + jy
v = np.stack([(px / W * 2 - 1) / p00, (1 - py / H * 2) / p11, np.ones_like(px)], 1)
print(f'frame {fr}: far routed px (z>1e5, every 2nd) {ok.sum()}, screen |d| p50 {np.median(np.hypot(sx, sy)):.2f}')
for name, M in [('v R1 R0^T (rows = axes)', R1 @ R0.T), ('v R1^T R0 (cols = axes)', R1.T @ R0)]:
    vp = v @ M
    rx = (vp[:, 0] / vp[:, 2] * p00 * 0.5 + 0.5) * W - px; ry = (0.5 - vp[:, 1] / vp[:, 2] * p11 * 0.5) * H - py
    print(f'  {name}: path |d| p50 {np.median(np.hypot(rx, ry)):.2f}, |motion - path| p50 {np.median(np.hypot(sx - rx, sy - ry)):.3f} p90 {np.percentile(np.hypot(sx - rx, sy - ry), 90):.3f}')
