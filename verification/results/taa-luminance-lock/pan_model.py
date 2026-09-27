#!/usr/bin/env python3
"""Section 12 of docs/architecture/taa-luminance-lock.md: what a world-static pan does to the lock, on the 8-phase raster
of the three stations (the card_raster.py prefix of ../lod-strut-widening, 'before' variant, three axis views).

Sequences of sub-pixel offsets (pixels): REST = the 8 Halton(2,3) phases (cyclic); PAN = 24 frames of the fractional part of
a 73.37 px/frame pan (0.37 n) in x plus the Halton phase (the camera gate's world-static case: the history is reprojected,
so per world point only the sub-pixel phase sequence changes). Per pixel (a world point in the reprojected frame):
  ripple: the std of the sample over the sequence (rest / pan);
  lock (rule resid2 of lock_share_model.py, floor TAU): residual e_n = q(sample_n) - q(bilinear(H, offset_n)), H the
  sequence mean image (the converged history), lock when e_n e_{n-1} < 0, max |e| >= max(TAU, RHO range3), min |e| >= TAU;
  rest lock set = from the REST sequence (what the flown build creates and carries), pan lock set = from the PAN sequence
  (what camera-gate creation would create under the pan);
  clip loss: for unlocked pixels with pan ripple, the share of pan frames in which H lies outside the current frame's
  3x3 box (the history is cut back) and outside the 7x7 box.
Prints per body / size: pan ripple energy on rest-locked pixels (carried) and on the rest; predicted pan ripple against
the blanket (all pixels at gain G_LOCK) for 'carry only' (rest-locked at G_LOCK, others at G_BASE) and 'pan creation'
(pan-locked at G_LOCK); pan lock share and plate false locks under the pan; clip losses.
Usage: pan_model.py WORKTREE NAME=s,... [--tau 3] [--rho 0.25] [--frames 24] [--vfrac 0.37]"""
import argparse, sys, time
from pathlib import Path
import numpy as np

HERE = Path(__file__).resolve().parent
SRC = (HERE.parent / 'lod-strut-widening' / 'card_raster.py').read_text()
PREFIX = SRC[:SRC.index('\nfor spec in args.specs:')]
ap = argparse.ArgumentParser()
ap.add_argument('worktree'); ap.add_argument('specs', nargs='+')
ap.add_argument('--rho', type=float, default=0.25); ap.add_argument('--tau', type=float, default=3.0)
ap.add_argument('--frames', type=int, default=24); ap.add_argument('--vfrac', type=float, default=0.37)
ap.add_argument('--axes', default='0,1,2')
ap.add_argument('--g-lock', type=float, default=0.020); ap.add_argument('--g-base', type=float, default=0.208)
opts = ap.parse_args()
sys.argv = [sys.argv[0], opts.worktree] + opts.specs + ['--variants', 'before', '--axes', opts.axes]
ns = {'__name__': 'card_raster_prefix'}
exec(compile(PREFIX, 'card_raster.py(prefix)', 'exec'), ns)
lod_raster, bob1, assets, FOCAL = ns['lod_raster'], ns['bob1'], ns['assets'], ns['FOCAL']
TAU, RHO = opts.tau / 255.0, opts.rho
HALTON = list(lod_raster.JITTER8)
PAN = [((HALTON[n % 8][0] + opts.vfrac * n) % 1.0 - 0.5, HALTON[n % 8][1]) for n in range(opts.frames)]


def box(img, r):
    p = np.pad(img, r, mode='edge')
    stack = np.stack([p[r + dy:p.shape[0] - r + dy, r + dx:p.shape[1] - r + dx]
                      for dy in range(-r, r + 1) for dx in range(-r, r + 1)])
    return stack.min(0), stack.max(0)


def shifted(img, sx, sy):
    ix, iy = int(np.floor(sx)), int(np.floor(sy))
    fx, fy = sx - ix, sy - iy
    r = lambda dx, dy: np.roll(np.roll(img, -(ix + dx), axis=1), -(iy + dy), axis=0)
    return (1 - fx) * (1 - fy) * r(0, 0) + fx * (1 - fy) * r(1, 0) + (1 - fx) * fy * r(0, 1) + fx * fy * r(1, 1)


qm = lambda a: np.round(a / (1 + a) * 255) / 255


def render_seq(tri, d, colour, alpha, blended, k, origin, size, seq):
    imgs, covs = [], []
    for jit in seq:
        r = lod_raster.render(tri, d, colour, alpha, blended, k, origin, size, jitter=jit)
        imgs.append(r['image']); covs.append(r['cover'] > 0)
    return np.stack(imgs), np.stack(covs)


def lock_set(imgs, seq):
    H = imgs.mean(0)
    q = qm(imgs)
    n = q.shape[0]
    e = np.stack([q[p] - qm(shifted(H, jx, jy)) for p, (jx, jy) in enumerate(seq)])
    lo, hi = zip(*[box(q[p], 1) for p in range(n)])
    tau = np.stack([np.maximum(TAU, RHO * (hi[p] - lo[p])) for p in range(n)])
    ev = np.zeros(q.shape, bool)
    for p in range(1, n):
        a0, a1 = np.abs(e[p]), np.abs(e[p - 1])
        ev[p] = (e[p] * e[p - 1] < 0) & (np.maximum(a0, a1) >= tau[p]) & (np.minimum(a0, a1) >= TAU)
    return ev.any(0), H


def stats(f, colour, alpha, blended, axis, k):
    tri, d, front = lod_raster.view(f, axis)
    origin, size = lod_raster.frame(tri[front], k)
    args = (tri[front], d[front], colour[front], alpha[front], blended[front], k, origin, size)
    rest_imgs, rest_covs = render_seq(*args, HALTON)
    pan_imgs, pan_covs = render_seq(*args, PAN)
    ever = rest_covs.any(0) | pan_covs.any(0)
    plate = rest_covs.all(0) & (rest_imgs.std(0) < 2 / 255)
    rest_locked, _ = lock_set(rest_imgs, HALTON)
    pan_locked, Hpan = lock_set(pan_imgs, PAN)
    rest_sd, pan_sd = rest_imgs.std(0), pan_imgs.std(0)
    flick = ever & (pan_sd >= 2 / 255) & ~rest_locked
    loss3 = loss7 = 0
    for p in range(pan_imgs.shape[0]):
        lo3, hi3 = box(pan_imgs[p], 1); lo7, hi7 = box(pan_imgs[p], 3)
        loss3 += ((Hpan < lo3) | (Hpan > hi3))[flick].sum()
        loss7 += ((Hpan < lo7) | (Hpan > hi7))[flick].sum()
    frames = pan_imgs.shape[0]
    return dict(px=int(ever.sum()), plate_px=int(plate.sum()),
                rest_e=float(rest_sd[ever].sum()), pan_e=float(pan_sd[ever].sum()),
                pan_e_rest_locked=float(pan_sd[rest_locked & ever].sum()),
                pan_e_pan_locked=float(pan_sd[pan_locked & ever].sum()),
                rest_locked=int((rest_locked & ever).sum()), pan_locked=int((pan_locked & ever).sum()),
                pan_plate_locked=int((pan_locked & plate).sum()), rest_plate_locked=int((rest_locked & plate).sum()),
                flick=int(flick.sum()) * frames, loss3=int(loss3), loss7=int(loss7),
                pan_flip_new=int((ever & (pan_sd >= 2 / 255) & (rest_sd < 2 / 255)).sum()))


for spec in opts.specs:
    name, ss = spec.split('='); sizes = [float(x) for x in ss.split(',')]
    t0 = time.time()
    tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, name)), 8)
    mats = bob1.materials(tree); ladder = bob1.lods(tree)
    src = ladder[0]
    r_raw = max(np.linalg.norm(p[1:4]) for p in src['points'] if p[0] & 1)
    model = ns['material_model'](mats, src)
    dens = ns['uv_density'](src)
    print(f'== {name} record 0: tau {opts.tau} rho {RHO} pan {opts.frames} frames at frac {opts.vfrac} px/frame gains lock {opts.g_lock} base {opts.g_base}')
    for s in sizes:
        k = FOCAL * s / (r_raw * 640)
        f, colour, alpha, blended = ns['face_arrays'](src, mats, model, k, dens)
        tot = None
        for axis in [int(x) for x in opts.axes.split(',')]:
            st = stats(f, colour, alpha, blended, axis, k)
            tot = st if tot is None else {key: tot[key] + st[key] for key in st}
        px = max(tot['px'], 1)
        blanket = opts.g_lock * tot['pan_e']
        carry = opts.g_lock * tot['pan_e_rest_locked'] + opts.g_base * (tot['pan_e'] - tot['pan_e_rest_locked'])
        create = opts.g_lock * tot['pan_e_pan_locked'] + opts.g_base * (tot['pan_e'] - tot['pan_e_pan_locked'])
        print(f'  s {s:g}: px {tot["px"]}, pan ripple energy / rest {tot["pan_e"] / max(tot["rest_e"], 1e-9):.2f}x; '
              f'pixels rippling under pan but quiet at rest {tot["pan_flip_new"] / px:.3f} of px')
        print(f'    rest lock set: share {tot["rest_locked"] / px:.3f}, pan energy on it {tot["pan_e_rest_locked"] / max(tot["pan_e"], 1e-9):.3f}; '
              f'predicted pan ripple vs blanket, carry only {carry / blanket:.2f}x')
        print(f'    pan lock set (camera-gate creation): share {tot["pan_locked"] / px:.3f}, pan energy on it {tot["pan_e_pan_locked"] / max(tot["pan_e"], 1e-9):.3f}, '
              f'plate locked {100 * tot["pan_plate_locked"] / max(tot["plate_px"], 1):.2f} % (rest {100 * tot["rest_plate_locked"] / max(tot["plate_px"], 1):.2f} %); '
              f'predicted pan ripple vs blanket {create / blanket:.2f}x')
        print(f'    clip loss on unlocked rippling px-frames under pan: 3x3 {tot["loss3"] / max(tot["flick"], 1):.3f}, 7x7 {tot["loss7"] / max(tot["flick"], 1):.3f}')
    print(f'  ({time.time() - t0:.0f} s)')
