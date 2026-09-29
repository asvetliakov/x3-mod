#!/usr/bin/env python3
"""Run 358 per-stage rest flicker on one crop (Run 100 A rest_flicker metric extended to every stage).
Mask: first frame's routed depth, view z in [zlo, zhi], inside the box, eroded 3 px (as rest_flicker_1080.py).
Per pair f-1 -> f, unwarped (camera still; the motion target is empty in run358): rms of display luma codes (255 * luma)
for present_ (after AgX + RCAS + bloom), AgX(taa_) (resolve output), AgX(hdr_) (unresolved current), color_ (bgra8 pre-TAA
input as dumped); share of present |d| > 4 codes. Lag-8 rms (f-8 -> f, same jitter index) for present and hdr.
Per frame: taa_age_ on the mask p50 and share == 1 (history rejected), lock share (taa_lock_ byte0 & 31 > 0),
motion target share with w != -1 (written by a routed draw).
Usage: stages.py <dir> first last x0 y0 x1 y1 zlo zhi [W H]"""
import glob, os, re, sys, numpy as np
sys.path.insert(0, '/Users/asvetl/x3-mod/verification/results/run340-run91a-pan-replay')
from replay_common import agx, LUMA
P22, P32 = 1.00000298, -6.00001812
d = sys.argv[1]; a, b = int(sys.argv[2]), int(sys.argv[3]); x0, y0, x1, y1 = map(int, sys.argv[4:8]); zlo, zhi = map(float, sys.argv[8:10])
W, H = (int(sys.argv[10]), int(sys.argv[11])) if len(sys.argv) > 11 else (5120, 1440)
L = glob.glob(f'{d}/session-*.log')[0]
evc = os.path.join(os.path.dirname(os.path.abspath(__file__)), f'ev_{os.path.basename(d)}.txt')
if not os.path.exists(evc):
    os.system(f"grep -E '^hdr_frame device=1 ' '{L}' | grep -oE 'frame=[0-9]+ |ev_adapted=[-0-9.]+' | paste - - > '{evc}'")
EV = {}
for l in open(evc):
    m = re.match(r'frame=(\d+)\s+ev_adapted=([-\d.]+)', l)
    if m: EV[int(m.group(1))] = float(m.group(2))
def mm(kind, f, ext, dt, ch): return np.memmap(f'{d}/{kind}_1_{f}.{ext}', dt, 'r', shape=(H, W, ch) if ch > 1 else (H, W))
z = np.array(mm('depth', a, 'rgba32f', np.float32, 4)[y0:y1, x0:x1, 0])
with np.errstate(divide='ignore', invalid='ignore'): vz = np.where(z >= 0, P32 / (z - P22), 0)
m = (vz >= zlo) & (vz <= zhi)
for _ in range(3):
    e = m.copy()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1): e &= np.roll(np.roll(m, dy, 0), dx, 1)
    m = e
def rgb8(kind, f): return (np.array(mm(kind, f, 'bgra8', np.uint8, 4)[y0:y1, x0:x1, :3])[..., ::-1] / 255.) @ LUMA * 255
def ag(kind, f): return agx(np.array(mm(kind, f, 'rgba16f', np.float16, 4)[y0:y1, x0:x1, :3]).astype(np.float64), EV[f]) @ LUMA * 255
S = {}
def st(f):
    if f not in S: S[f] = {'pres': rgb8('present', f), 'taa': ag('taa', f), 'hdr': ag('hdr', f), 'color': rgb8('color', f)}
    return S[f]
r = lambda x: float(np.sqrt((x[m] ** 2).mean()))
rows = []
print(f'mask px {int(m.sum())} box {x0},{y0}-{x1},{y1} z {zlo:.0f}-{zhi:.0f}')
for f in range(a, b + 1):
    age = np.array(mm('taa_age', f, 'r32f', np.float32, 1)[y0:y1, x0:x1])[m]
    lk = np.array(mm('taa_lock', f, 'bgra8', np.uint8, 4)[y0:y1, x0:x1, 0])[m] & 31
    mw = np.array(mm('motion', f, 'rgba32f', np.float32, 4)[y0:y1, x0:x1, 3])[m]
    s = f'frame {f}: age p50 {np.median(age):.0f} ==1 {100*(age <= 1).mean():.1f} % | lock {100*(lk > 0).mean():.2f} % | motion written {100*(mw != -1).mean():.1f} %'
    if f > a:
        c, p = st(f), st(f - 1)
        dp = c['pres'] - p['pres']
        v = [r(c[k] - p[k]) for k in ('pres', 'taa', 'hdr', 'color')]
        s += f' | pair rms present {v[0]:.2f} (>4 {100*(np.abs(dp[m]) > 4).mean():.1f} %) taa {v[1]:.2f} hdr {v[2]:.2f} color {v[3]:.2f}'
        l8 = (float('nan'),) * 2
        if f - 8 >= a:
            q = st(f - 8); l8 = (r(c['pres'] - q['pres']), r(c['hdr'] - q['hdr']))
            s += f' | lag8 present {l8[0]:.2f} hdr {l8[1]:.2f}'
        rows.append(v + [100*(np.abs(dp[m]) > 4).mean()] + list(l8) + [100*(age <= 1).mean(), 100*(lk > 0).mean()])
    for k in [k for k in S if k < f - 8]: del S[k]
    print(s, flush=True)
R = np.array(rows)
med = lambda i: np.nanmedian(R[:, i])
print(f'MEDIAN pairs: present rms {med(0):.3f} >4 {med(4):.2f} % | taa {med(1):.3f} | hdr {med(2):.3f} | color {med(3):.3f} | lag8 present {med(5):.3f} hdr {med(6):.3f} | age==1 {med(7):.1f} % | lock {med(8):.2f} %')
