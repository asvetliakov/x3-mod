#!/usr/bin/env python3
"""Counterfactual replay on captured frames: history seeded with taa_(first) (the game's own w 0.9 resolve), then frames
first+1..last resolved by replay_common.resolve with keep = w on the station's base pixels (region hold 0, far weight 0; other
pixels keep the captured resolve), the crop's own output fed back as history (outside the crop: the captured taa_).
Metric per frame on station interior base pixels: E(AgX(out)) / E(AgX(hdr_)) (run254 hull-sharpness ratio), and after
RCAS at sharpen 0.75 / 1.0; flicker proxy: display-code change of out between consecutive frames at the same scene point is
not available at these burst speeds, so the second metric is the residual |out - current| p50 in codes (history share).
Usage: replay_real.py <dir> first last x0 y0 x1 y1 zlo zhi"""
import os, re, subprocess, sys, numpy as np
from replay_common import *
d = sys.argv[1]; first, last = int(sys.argv[2]), int(sys.argv[3]); x0, y0, x1, y1 = map(int, sys.argv[4:8]); zlo, zhi = map(float, sys.argv[8:10])
log = [p for p in os.listdir(d) if p.startswith('session-')][0]
def meta(f):
    r = subprocess.run(['grep', '-m1', '-E', r'^motion_output_frame device=1 frame=%d ' % f, f'{d}/{log}'], capture_output=True, text=True).stdout
    e = subprocess.run(['grep', '-m1', '-E', r'^hdr_frame device=1 frame=%d ' % f, f'{d}/{log}'], capture_output=True, text=True).stdout
    kv = dict(re.findall(r'(\w+)=([-\w.+]+)', r))
    return float(kv['taa_k']), float(kv['jitter_x']), float(kv['jitter_y']), float(re.search(r'ev_adapted=([-\d.]+)', e).group(1))
def full(kind, f, ext, dt, ch): return np.memmap(f'{d}/{kind}_1_{f}.{ext}', dt, 'r', shape=(H, W, ch))
results = {}
for w in (0.9, 0.85, 0.8):
    hist = np.array(full('taa', first, 'rgba16f', np.float16, 4)[..., :3]).astype(np.float64)
    rows = []
    for f in range(first + 1, last + 1):
        k, jx, jy, ev = meta(f)
        cur = np.array(full('hdr', f, 'rgba16f', np.float16, 4)[y0 - 1:y1 + 1, x0 - 1:x1 + 1, :3]).astype(np.float64)
        mot = np.array(full('motion', f, 'rgba32f', np.float32, 4)[y0:y1, x0:x1]).astype(np.float64)
        cap = np.array(full('taa', f, 'rgba16f', np.float16, 4)[..., :3]).astype(np.float64)
        z = np.array(full('depth', f, 'rgba32f', np.float32, 4)[y0:y1, x0:x1, 0])
        age = np.abs(np.fromfile(f'{d}/taa_age_1_{f}.r32f', np.float32).reshape(H, W)[y0:y1, x0:x1])
        hh = np.round(np.where(age <= 65, np.modf(age)[0] * 65536, 0)) % 128
        with np.errstate(divide='ignore', invalid='ignore'):
            vz = np.where(z >= 0, P32 / (z - P22), 0)
        base = (vz >= zlo) & (vz <= zhi) & (hh < 0.5) & (mot[..., 3] == 1) & (np.floor(age) > 1)
        px = mot[..., 0] * W - 0.5 + jx; py = mot[..., 1] * H - 0.5 + jy
        out = resolve(cur, hist, px, py, w, k)
        crop_cap = cap[y0:y1, x0:x1]
        out = np.where(base[..., None], out, crop_cap)
        inter = base.copy()
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1): inter &= np.roll(np.roll(base, dy, 0), dx, 1)
        Ac = agx(cur[1:-1, 1:-1], ev); Ao = agx(out, ev); E0 = energy(Ac, inter)
        mv = np.hypot(mot[..., 0] * W - 0.5 - np.arange(x0, x1)[None], mot[..., 1] * H - 0.5 - np.arange(y0, y1)[:, None])[inter]
        rows.append((f, np.median(mv), energy(Ao, inter) / E0, energy(rcas(Ao, 0.75), inter) / E0, energy(rcas(Ao, 1.0), inter) / E0, int(inter.sum())))
        hist = cap; hist[y0:y1, x0:x1] = out
    results[w] = rows
for w, rows in results.items():
    for f, mv, e, e75, e100, n in rows:
        print(f'w {w:.2f} frame {f} |d| p50 {mv:6.1f} px interior {n:6d}: E_taa/E_cur {e:.3f}  RCAS0.75 {e75:.3f}  RCAS1.0 {e100:.3f}')
