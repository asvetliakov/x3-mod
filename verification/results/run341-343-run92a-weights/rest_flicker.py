#!/usr/bin/env python3
"""Run 92 A rest flicker: frame-to-frame change of the presented image (present_, after AgX + RCAS) on the station interior,
for consecutive capture frames f-1 -> f of a rest burst, and the same for the unresolved current frame (AgX of hdr_) as the
normaliser (the jitter's raw aliasing, identical between sessions for the same stand). Station mask from the first frame's
depth (routed, view depth in [zlo, zhi], box), eroded by 3 px against edges moving in and out. Luma codes = 255 * display
luma. Also the station's median RT1 displacement of frame f (a drifting view inflates both numbers alike).
Usage: rest_flicker.py <dir> first count x0 y0 x1 y1 zlo zhi"""
import os, re, subprocess, sys, numpy as np
sys.path.insert(0, '/Users/asvetl/x3-mod/verification/results/run340-run91a-pan-replay')
from replay_common import *
d = sys.argv[1]; first, count = int(sys.argv[2]), int(sys.argv[3]); x0, y0, x1, y1 = map(int, sys.argv[4:8]); zlo, zhi = map(float, sys.argv[8:10])
log = [p for p in os.listdir(d) if p.startswith('session-')][0]
def ev(f):
    e = subprocess.run(['grep', '-m1', '-E', r'^hdr_frame device=1 frame=%d ' % f, f'{d}/{log}'], capture_output=True, text=True).stdout
    return float(re.search(r'ev_adapted=([-\d.]+)', e).group(1))
def full(kind, f, ext, dt, ch): return np.memmap(f'{d}/{kind}_1_{f}.{ext}', dt, 'r', shape=(H, W, ch))
z = np.array(full('depth', first, 'rgba32f', np.float32, 4)[y0:y1, x0:x1, 0])
with np.errstate(divide='ignore', invalid='ignore'):
    vz = np.where(z >= 0, P32 / (z - P22), 0)
m = (vz >= zlo) & (vz <= zhi)
for _ in range(3):
    e = m.copy()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1): e &= np.roll(np.roll(m, dy, 0), dx, 1)
    m = e
def pres(f): return (np.array(full('present', f, 'bgra8', np.uint8, 4)[y0:y1, x0:x1, :3])[..., ::-1] / 255.) @ LUMA * 255
def cur(f): return agx(np.array(full('hdr', f, 'rgba16f', np.float16, 4)[y0:y1, x0:x1, :3]).astype(np.float64), ev(f)) @ LUMA * 255
ys, xs = np.mgrid[y0:y1, x0:x1]
rows = []
for f in range(first + 1, first + count):
    mo = np.array(full('motion', f, 'rgba32f', np.float32, 4)[y0:y1, x0:x1])
    disp = np.median(np.hypot(mo[..., 0] * W - 0.5 - xs, mo[..., 1] * H - 0.5 - ys)[m])
    dp = (pres(f) - pres(f - 1))[m]; dc = (cur(f) - cur(f - 1))[m]
    rp, rc = np.sqrt((dp ** 2).mean()), np.sqrt((dc ** 2).mean())
    rows.append((f, disp, rp, np.percentile(np.abs(dp), 99), (np.abs(dp) > 4).mean(), rc, rp / rc))
    print(f'pair {f-1}->{f} |d| p50 {disp:.2f}: present rms {rp:.3f} codes p99 {np.percentile(np.abs(dp), 99):.2f} >4 codes {100*(np.abs(dp) > 4).mean():.3f} % | current rms {rc:.3f} | present/current {rp/rc:.4f}')
print(f'station px {int(m.sum())}; first pair (normal-rate): present rms {rows[0][2]:.3f}, ratio {rows[0][6]:.4f}; median over pairs: present rms {np.median([r[2] for r in rows]):.3f}, ratio {np.median([r[6] for r in rows]):.4f}')
