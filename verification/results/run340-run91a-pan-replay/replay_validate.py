#!/usr/bin/env python3
"""One-step validation of replay_common.resolve against the captured resolve: history = taa_(f-1), current = hdr_(f),
previous position from motion_(f) (UV incl. half texel) plus a jitter term, keep 0.9; compared with taa_(f) on the station's
base pixels (routed depth in [zlo, zhi], region hold h = 0 in taa_age_(f), valid motion). Tries the jitter-term variants
(sizeJitter.zw) and prints the display-code error (AgX luma * 255) per variant.
Usage: replay_validate.py <dir> <frame> x0 y0 x1 y1 zlo zhi"""
import os, re, subprocess, sys, numpy as np
from replay_common import *
d, fr = sys.argv[1], int(sys.argv[2]); x0, y0, x1, y1 = map(int, sys.argv[3:7]); zlo, zhi = map(float, sys.argv[7:9])
log = [p for p in os.listdir(d) if p.startswith('session-')][0]
def meta(f):
    r = subprocess.run(['grep', '-m1', '-E', r'^motion_output_frame device=1 frame=%d ' % f, f'{d}/{log}'], capture_output=True, text=True).stdout
    e = subprocess.run(['grep', '-m1', '-E', r'^hdr_frame device=1 frame=%d ' % f, f'{d}/{log}'], capture_output=True, text=True).stdout
    kv = dict(re.findall(r'(\w+)=([-\w.+]+)', r))
    return float(kv['taa_k']), float(kv['jitter_x']), float(kv['jitter_y']), float(kv['jitter_previous_x']), float(kv['jitter_previous_y']), float(re.search(r'ev_adapted=([-\d.]+)', e).group(1))
k, jx, jy, pjx, pjy, ev = meta(fr)
def full(kind, f, ext, dt, ch): return np.memmap(f'{d}/{kind}_1_{f}.{ext}', dt, 'r', shape=(H, W, ch))
hist = np.array(full('taa', fr - 1, 'rgba16f', np.float16, 4)[..., :3]).astype(np.float64)
cur = np.array(full('hdr', fr, 'rgba16f', np.float16, 4)[y0 - 1:y1 + 1, x0 - 1:x1 + 1, :3]).astype(np.float64)
mot = np.array(full('motion', fr, 'rgba32f', np.float32, 4)[y0:y1, x0:x1]).astype(np.float64)
ref = np.array(full('taa', fr, 'rgba16f', np.float16, 4)[y0:y1, x0:x1, :3]).astype(np.float64)
z = np.array(full('depth', fr, 'rgba32f', np.float32, 4)[y0:y1, x0:x1, 0])
age = np.abs(np.fromfile(f'{d}/taa_age_1_{fr}.r32f', np.float32).reshape(H, W)[y0:y1, x0:x1])
h = np.round(np.where(age <= 65, np.modf(age)[0] * 65536, 0)) % 128
with np.errstate(divide='ignore', invalid='ignore'):
    vz = np.where(z >= 0, P32 / (z - P22), 0)
base = (vz >= zlo) & (vz <= zhi) & (h < 0.5) & (mot[..., 3] == 1) & (np.floor(age) > 1)
A_ref = agx(ref, ev) @ LUMA * 255
for name, (ox, oy) in {'0': (0, 0), '+j': (jx, jy), '+j,-y': (jx, -jy), '-j': (-jx, -jy), '-j,+y': (-jx, jy), '+pj': (pjx, pjy), '+pj,-y': (pjx, -pjy)}.items():
    px = mot[..., 0] * W - 0.5 + ox; py = mot[..., 1] * H - 0.5 + oy
    out = resolve(cur, hist, px, py, 0.9, k)
    e = np.abs(agx(out, ev) @ LUMA * 255 - A_ref)[base]
    print(f'frame {fr} variant {name:7s} base px {int(base.sum())}: err codes p50 {np.median(e):.3f} p90 {np.percentile(e, 90):.3f} p99 {np.percentile(e, 99):.2f} mean {e.mean():.3f}')
