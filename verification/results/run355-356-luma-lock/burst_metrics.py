#!/usr/bin/env python3
"""Run 355/356 per-frame metrics on the far spacedock (view z 55-100k) and the far outpost (z 120-190k), 1920x1080.
Object mask: routed depth in the z range inside the burst's union box (boxes_out.txt, +12 px), eroded 1 px (the spacedock is a strut lattice).
Per pair f-1 -> f: RT1 |d| p50 px; reprojected frame-to-frame change of present_ (display luma codes, present(f-1)
bilinear at the RT1 previous position) rms and share > 4 codes; same for AgX(hdr_) (unresolved current) as normaliser.
Per frame: sharpness E_present/E_cur and E_taa/E_cur (sharpness_measured_1080.py metric). Run 356: lock lane (taa_lock_,
bgra8 row major, byte 0 = lane b = t + 32 class + 64 sign + 128 mode; lock = t > 0) shares on the object, its flat
interior (3x3 range of AgX(hdr) luma < 4 codes: plate proxy), the sky (depth -1) and the frame; mean t of locks.
Usage: burst_metrics.py <dir> first count"""
import os, re, subprocess, sys, numpy as np
sys.path.insert(0, '/Users/asvetl/x3-mod/verification/results/run340-run91a-pan-replay')
from replay_common import agx, bilinear, LUMA
W, H = 1920, 1080; P22, P32 = 1.00000298, -6.00001812
d, first, count = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
log = [p for p in os.listdir(d) if p.startswith('session-')][0]
def ev(f):
    e = subprocess.run(['grep', '-m1', '-E', r'^hdr_frame device=1 frame=%d ' % f, f'{d}/{log}'], capture_output=True, text=True).stdout
    return float(re.search(r'ev_adapted=([-\d.]+)', e).group(1))
def mm(kind, f, ext, dt, ch): return np.memmap(f'{d}/{kind}_1_{f}.{ext}', dt, 'r', shape=(H, W, ch))
boxes = {}
for l in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'boxes_out.txt')):
    f, k, _, b, _, z = l.split(); x0, y0, x1, y1 = map(int, re.split('[,-]', b)); boxes.setdefault(k, []).append((int(f), x0, y0, x1, y1))
frames = range(first, first + count)
def ubox(k):
    bs = [b[1:] for b in boxes[k] if first <= b[0] < first + count]
    return max(min(b[0] for b in bs) - 12, 0), max(min(b[1] for b in bs) - 12, 0), min(max(b[2] for b in bs) + 12, W), min(max(b[3] for b in bs) + 12, H)
OBJ = {'spacedock': (55000, 100000), 'far_outpost': (120000, 190000)}
def erode(m, n=3):
    for _ in range(n):
        e = m.copy()
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1): e &= np.roll(np.roll(m, dy, 0), dx, 1)
        m = e
    return m
def rng5(l):
    mx = l.copy(); mn = l.copy()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            s = np.roll(np.roll(l, dy, 0), dx, 1); mx = np.maximum(mx, s); mn = np.minimum(mn, s)
    return mx - mn
ys, xs = np.mgrid[0:H, 0:W]
cache = {}
def frame(f):
    if f in cache: return cache[f]
    z = np.array(mm('depth', f, 'rgba32f', np.float32, 4)[:, :, 0])
    with np.errstate(divide='ignore', invalid='ignore'): vz = np.where(z >= 0, P32 / (z - P22), 0)
    e = ev(f)
    pres = (np.array(mm('present', f, 'bgra8', np.uint8, 4)[:, :, :3])[..., ::-1] / 255.) @ LUMA * 255
    cur = agx(np.array(mm('hdr', f, 'rgba16f', np.float16, 4)[:, :, :3]).astype(np.float64), e)
    taa = agx(np.array(mm('taa', f, 'rgba16f', np.float16, 4)[:, :, :3]).astype(np.float64), e)
    mo = np.array(mm('motion', f, 'rgba32f', np.float32, 4)[:, :, :2])
    lk = np.array(mm('taa_lock', f, 'bgra8', np.uint8, 4)[:, :, 0]) if os.path.exists(f'{d}/taa_lock_1_{f}.bgra8') else None
    cache.clear(); cache[f] = (z, vz, pres, cur @ LUMA * 255, taa @ LUMA * 255, mo, lk); return cache[f]
def energy(l, m):
    gx = np.zeros_like(l); gy = np.zeros_like(l); gx[:, :-1] = l[:, 1:] - l[:, :-1]; gy[:-1] = l[1:] - l[:-1]
    return float(((gx ** 2 + gy ** 2)[m]).mean())
prev = None
for f in frames:
    z, vz, pres, cur, taa, mo, lk = frame(f)
    px, py = mo[..., 0] * W - 0.5, mo[..., 1] * H - 0.5
    out = [f'frame {f}']
    masks = {}
    for k, (lo, hi) in OBJ.items():
        x0, y0, x1, y1 = ubox(k); m = np.zeros((H, W), bool); m[y0:y1, x0:x1] = True; m &= (vz >= lo) & (vz <= hi); m = erode(m, 1); masks[k] = m
        disp = np.median(np.hypot(px - xs, py - ys)[m]); s = f'{k} px {int(m.sum())} |d| {disp:.2f}'
        s += f' sharp pres/cur {energy(pres / 255, m) / energy(cur / 255, m):.3f} taa/cur {energy(taa / 255, m) / energy(cur / 255, m):.3f}'
        if prev is not None:
            pp, pc = prev
            ix, iy = np.clip(np.rint(px).astype(int), 0, W - 1), np.clip(np.rint(py).astype(int), 0, H - 1)
            v = m & (px >= 0) & (px < W - 1) & (py >= 0) & (py < H - 1)
            dp = (pres - bilinear(pp[..., None], px, py)[..., 0])[v]; dc = (cur - bilinear(pc[..., None], px, py)[..., 0])[v]
            rp, rc = np.sqrt((dp ** 2).mean()), np.sqrt((dc ** 2).mean())
            s += f' | pair rms pres {rp:.3f} >4 {100 * (np.abs(dp) > 4).mean():.2f} % cur {rc:.3f} ratio {rp / rc:.3f}'
        if lk is not None:
            t = lk & 31; L = t > 0; flat = m & (rng5(cur) < 4)
            s += f' | lock {100 * L[m].mean():.2f} % flat {100 * L[flat].mean():.2f} % (flat px {int(flat.sum())}) t_mean {t[m & L].mean() if (m & L).any() else 0:.1f} motion-mode {100 * (lk[m] >= 128).mean():.1f} %'
        out.append(s)
    if lk is not None:
        t = lk & 31; sky = z < 0
        out.append(f'frame lock {100 * (t > 0).mean():.3f} % sky lock {100 * (t[sky] > 0).mean():.4f} % (sky px {int(sky.sum())}) motion-mode {100 * (lk >= 128).mean():.1f} %')
    print('\n  '.join(out), flush=True)
    prev = (pres, cur)
