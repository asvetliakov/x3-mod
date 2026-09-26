#!/usr/bin/env python3
"""Effective history weight actually applied, from the dumps: frame f's resolve is re-run with replay_common (history =
captured taa_(f-1), current = hdr_(f), motion_(f) + current jitter, 3x3 clip) to get the clipped history `old`; per pixel
w = <res - cur, old - cur> / |old - cur|^2 in the weighed domain (res = captured taa_(f)), on ordinary station pixels
(routed depth in [zlo, zhi], region hold 0, age > 1) where |old - cur| is at least 0.02 in weighed luma; prints the median
and quartiles, plus the scalar keep in 0.60..0.97 that best reproduces taa_(f) (display codes, median abs error) and the
rotation-only turn r (px/frame) of those pixels from camera_state (as camera_parallax.py, conv 1).
Usage: effective_weight.py <dir> <frame> x0 y0 x1 y1 zlo zhi"""
import os, re, subprocess, sys, numpy as np
sys.path.insert(0, '/Users/asvetl/x3-mod/verification/results/run340-run91a-pan-replay')
from replay_common import *
import replay_common as rc
d, fr = sys.argv[1], int(sys.argv[2]); x0, y0, x1, y1 = map(int, sys.argv[3:7]); zlo, zhi = map(float, sys.argv[7:9])
log = [p for p in os.listdir(d) if p.startswith('session-')][0]
g = lambda pat: subprocess.run(['grep', '-m1', '-E', pat, f'{d}/{log}'], capture_output=True, text=True).stdout
kv = dict(re.findall(r'(\w+)=([-\w.+]+)', g(r'^motion_output_frame device=1 frame=%d ' % fr)))
k, jx, jy = float(kv['taa_k']), float(kv['jitter_x']), float(kv['jitter_y'])
ev = float(re.search(r'ev_adapted=([-\d.]+)', g(r'^hdr_frame device=1 frame=%d ' % fr)).group(1))
cam = {}
for f in (fr, fr - 1):
    c = dict(re.findall(r'(\w+)=([-\w.+]+)', g(r'^camera_state device=1 frame=%d ' % f)))
    cam[f] = ([float(c[x]) for x in ('p00', 'p11', 'p20', 'p21')], np.array([[float(c['r%d%d' % (i, j)]) for j in range(3)] for i in range(3)]))
def full(kind, f, ext, dt, ch): return np.memmap(f'{d}/{kind}_1_{f}.{ext}', dt, 'r', shape=(H, W, ch))
hist = np.array(full('taa', fr - 1, 'rgba16f', np.float16, 4)[..., :3]).astype(np.float64)
cur = np.array(full('hdr', fr, 'rgba16f', np.float16, 4)[y0 - 1:y1 + 1, x0 - 1:x1 + 1, :3]).astype(np.float64)
mot = np.array(full('motion', fr, 'rgba32f', np.float32, 4)[y0:y1, x0:x1]).astype(np.float64)
res = np.array(full('taa', fr, 'rgba16f', np.float16, 4)[y0:y1, x0:x1, :3]).astype(np.float64)
z = np.array(full('depth', fr, 'rgba32f', np.float32, 4)[y0:y1, x0:x1, 0])
age = np.abs(np.fromfile(f'{d}/taa_age_1_{fr}.r32f', np.float32).reshape(H, W)[y0:y1, x0:x1])
hh = np.round(np.where(age <= 65, np.modf(age)[0] * 65536, 0)) % 128
with np.errstate(divide='ignore', invalid='ignore'):
    vz = np.where(z >= 0, P32 / (z - P22), 0)
base = (vz >= zlo) & (vz <= zhi) & (hh < 0.5) & (mot[..., 3] == 1) & (np.floor(age) > 1)
px = mot[..., 0] * W - 0.5 + jx; py = mot[..., 1] * H - 0.5 + jy
# the clipped history, reproduced inside resolve(): keep = 1 returns it (unweighed)
old = rc.resolve(cur, hist, px, py, 1.0, k)
wc, wo, wr = weigh(cur[1:-1, 1:-1], k), weigh(old, k), weigh(res, k)
num = ((wr - wc) * (wo - wc)).sum(-1); den = ((wo - wc) ** 2).sum(-1)
sel = base & (np.sqrt(den) >= 0.02)
wpix = num[sel] / den[sel]
# rotation-only turn r at these pixels
ys, xs = np.mgrid[y0:y1, x0:x1]
(p00, p11, p20, p21), Rc = cam[fr]; (q00, q11, q20, q21), Rp = cam[fr - 1]
dv = np.stack([(2 * xs / W - 1 - p20) / p00, (1 - 2 * ys / H - p21) / p11, np.ones(xs.shape)], -1)
pv = (dv @ Rc.T) @ Rp
rx = (pv[..., 0] / pv[..., 2] * q00 + q20 + 1) / 2 * W; ry = (1 - (pv[..., 1] / pv[..., 2] * q11 + q21)) / 2 * H
turn = np.hypot(rx - xs, ry - ys)[base]
A_ref = agx(res, ev) @ LUMA * 255
best = min(((np.median(np.abs(agx(rc.resolve(cur, hist, px, py, w, k), ev) @ LUMA * 255 - A_ref)[base]), w) for w in np.arange(0.60, 0.975, 0.01)))
print(f'frame {fr} box {x0},{y0},{x1},{y1} z {zlo:.0f}-{zhi:.0f}: ordinary px {int(base.sum())}, turn r p50 {np.median(turn):.2f} px/frame (p10 {np.percentile(turn,10):.2f}, p90 {np.percentile(turn,90):.2f}); '
      f'per-pixel w on {int(sel.sum())} px: p25 {np.percentile(wpix,25):.3f} p50 {np.median(wpix):.3f} p75 {np.percentile(wpix,75):.3f}; best scalar keep {best[1]:.2f} (median err {best[0]:.3f} codes)')
