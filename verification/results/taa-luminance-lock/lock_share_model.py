#!/usr/bin/env python3
"""Lock-detector model on the 8-phase orthographic raster of a body's record 0 (the source of C), for the design note
docs/architecture/taa-luminance-lock.md. Reuses the helpers of ../lod-strut-widening/card_raster.py (material model,
face arrays, lod_raster.render of the strut-widening worktree) by exec of that file's prefix (everything before its
main loop); the 'before' variant only (no widening).

Per pixel, the 8 Halton phases stand in for 8 consecutive frames at rest (cyclic). Luma is tone-mapped q = L / (1 + L)
and quantised to 1/255 as the lane would store it. Rules evaluated at each phase p:
  flip:  d0 = q_p - q_{p-1}, d1 = q_{p-1} - q_{p-2}; d0 * d1 < 0 and |d0| >= tau_p and |d1| >= tau_{p-1},
         tau = max(TAU_ABS, RHO * range3) with range3 the 3x3 luma range of that phase's image (neighbourhood-normalised).
  ridge: FSR2's ComputeThinFeatureConfidence (ffx_fsr2_lock.h, MIT): a neighbour is 'similar' when its luma is within
         5 % (ratio < 1.05; here with TAU_ABS added so black-on-black is similar); the centre must be above every
         dissimilar neighbour or below every one (a ridge, at least one dissimilar), and no 2x2 quad containing the centre
         may be all similar (a feature thinner than 2 px in every quadrant).
  both:  flip and ridge in the same phase.
  resid: (section 10) the residual e_p = q_p - q(H at the jittered position), H the converged rest history modelled as the
         8-phase mean image, bilinearly interpolated at pixel + jitter_p (lod_raster.render shifts the geometry by -jitter,
         so phase p samples the scene at pixel centre + jitter_p); lock when e_p * e_{p-1} < 0 with both above tau.
  resid2: the residual test asymmetric: e_p * e_{p-1} < 0, max(|e_p|, |e_{p-1}|) >= tau, min(...) >= TAU_ABS (a rarely
         covered strut pixel has a small residual on its uncovered frames).
  flipveto: the raw flip with the residual as a veto: flip and max(|e_p|, |e_{p-1}|) >= tau.
A pixel is 'locked' by a rule if the event occurs in at least one of the 8 phases (lifetime >= one jitter period keeps it
locked the whole cycle). Reported per body / size, pixel-weighted over the three axis views: lock share of the ever-covered
pixels, the share of the summed per-pixel 8-phase std (ripple energy) the locked pixels carry, the plate share locked
(plate = always covered and std < 2/255: a false lock), the refresh count (events per locked pixel per cycle), and the
predicted rest ripple relative to today's blanket 0.985 with locked pixels at gain G_LOCK and the rest at G_BASE
(closed-form 8-phase fundamental gains of taa-distant-line-fade.md: 0.020 at 0.985, 0.208 at 0.85).
Usage: lock_share_model.py WORKTREE NAME=s,... [--rho 0.25] [--tau 2] [--axes 0,1,2]"""
import argparse, sys, time
from pathlib import Path
import numpy as np

HERE = Path(__file__).resolve().parent
SRC = (HERE.parent / 'lod-strut-widening' / 'card_raster.py').read_text()
PREFIX = SRC[:SRC.index('\nfor spec in args.specs:')]
ap = argparse.ArgumentParser()
ap.add_argument('worktree'); ap.add_argument('specs', nargs='+')
ap.add_argument('--rho', type=float, default=0.25); ap.add_argument('--tau', type=float, default=2.0, help='codes of 255')
ap.add_argument('--axes', default='0,1,2')
ap.add_argument('--g-lock', type=float, default=0.020); ap.add_argument('--g-base', type=float, default=0.208)
opts = ap.parse_args()
sys.argv = [sys.argv[0], opts.worktree] + opts.specs + ['--variants', 'before', '--axes', opts.axes]
ns = {'__name__': 'card_raster_prefix'}
exec(compile(PREFIX, 'card_raster.py(prefix)', 'exec'), ns)
lod_raster, bob1, assets = ns['lod_raster'], ns['bob1'], ns['assets']
FOCAL = ns['FOCAL']
TAU_ABS, RHO = opts.tau / 255.0, opts.rho


QUADS = ((0, 1, 3), (1, 2, 4), (3, 5, 6), (4, 6, 7))  # neighbour indices (row-major, centre skipped) of the four 2x2 quads


def neighbours(img):
    """FSR2 ridge flag, and the 3x3 luma range including the centre (edge-replicated)."""
    p = np.pad(img, 1, mode='edge')
    stack = np.stack([p[1 + dy:p.shape[0] - 1 + dy, 1 + dx:p.shape[1] - 1 + dx]
                      for dy in (-1, 0, 1) for dx in (-1, 0, 1) if (dy, dx) != (0, 0)])
    similar = np.abs(stack - img) <= 0.05 * np.maximum(stack, img) + TAU_ABS
    dis = ~similar
    big = np.where(dis, stack, -np.inf).max(0); small = np.where(dis, stack, np.inf).min(0)
    ridge = dis.any(0) & ((img > big) | (img < small))
    for qd in QUADS:
        ridge &= ~(similar[qd[0]] & similar[qd[1]] & similar[qd[2]])
    return ridge, np.maximum(stack.max(0), img) - np.minimum(stack.min(0), img)


def shifted(img, sx, sy):
    """Bilinear sample of img at (x + sx, y + sy) for every pixel (wrapping at the frame edge, which is background)."""
    ix, iy = int(np.floor(sx)), int(np.floor(sy))
    fx, fy = sx - ix, sy - iy
    r = lambda dx, dy: np.roll(np.roll(img, -(ix + dx), axis=1), -(iy + dy), axis=0)
    return (1 - fx) * (1 - fy) * r(0, 0) + fx * (1 - fy) * r(1, 0) + (1 - fx) * fy * r(0, 1) + fx * fy * r(1, 1)


def stats_lock(f, colour, alpha, blended, axis, k):
    tri, d, front = lod_raster.view(f, axis)
    origin, size = lod_raster.frame(tri[front], k)
    imgs, covs = [], []
    for jit in lod_raster.JITTER8:
        r = lod_raster.render(tri[front], d[front], colour[front], alpha[front], blended[front], k, origin, size, jitter=jit)
        imgs.append(r['image']); covs.append(r['cover'] > 0)
    imgs, covs = np.stack(imgs), np.stack(covs)
    ever, always = covs.any(0), covs.all(0)
    q = np.round(imgs / (1 + imgs) * 255) / 255
    sd = imgs.std(0)
    n = q.shape[0]
    flip = np.zeros(q.shape, bool); ridge = np.zeros(q.shape, bool); tau = np.zeros(q.shape)
    for p in range(n):
        ridge[p], rng = neighbours(q[p])
        tau[p] = np.maximum(TAU_ABS, RHO * rng)
    for p in range(n):
        d0 = q[p] - q[p - 1]; d1 = q[p - 1] - q[p - 2]
        flip[p] = (d0 * d1 < 0) & (np.abs(d0) >= tau[p]) & (np.abs(d1) >= tau[p - 1])
    both = flip & ridge
    H = imgs.mean(0)
    e = np.stack([q[p] - np.round(shifted(H, jx, jy) / (1 + shifted(H, jx, jy)) * 255) / 255
                  for p, (jx, jy) in enumerate(lod_raster.JITTER8)])
    resid = np.zeros(q.shape, bool); resid2 = np.zeros(q.shape, bool); flipveto = np.zeros(q.shape, bool)
    for p in range(n):
        a0, a1 = np.abs(e[p]), np.abs(e[p - 1])
        opp = e[p] * e[p - 1] < 0
        resid[p] = opp & (a0 >= tau[p]) & (a1 >= tau[p - 1])
        resid2[p] = opp & (np.maximum(a0, a1) >= tau[p]) & (np.minimum(a0, a1) >= TAU_ABS)
        flipveto[p] = flip[p] & (np.maximum(a0, a1) >= tau[p])
    plate = always & (sd < 2 / 255)
    out = dict(px=int(ever.sum()), flip_share=float((ever & ~always).sum()), sd_sum=float(sd[ever].sum()),
               plate_px=int(plate.sum()))
    for name, ev in (('flip', flip), ('ridge', ridge), ('both', both), ('resid', resid), ('resid2', resid2), ('flipveto', flipveto)):
        locked = ev.any(0) & ever
        events = ev.sum(0)[locked]
        out[name] = dict(locked=int(locked.sum()), sd_locked=float(sd[locked].sum()),
                         plate_locked=int((locked & plate).sum()),
                         refresh2=int((events >= 2).sum()),
                         predicted=float(opts.g_lock * sd[locked].sum() + opts.g_base * sd[ever & ~locked].sum()))
    return out


for spec in opts.specs:
    name, ss = spec.split('='); sizes = [float(x) for x in ss.split(',')]
    t0 = time.time()
    tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, name)), 8)
    mats = bob1.materials(tree); ladder = bob1.lods(tree)
    src = ladder[0]
    r_raw = max(np.linalg.norm(p[1:4]) for p in src['points'] if p[0] & 1)
    model = ns['material_model'](mats, src)
    dens = ns['uv_density'](src)
    print(f'== {name} record 0 rho {RHO} tau {opts.tau}/255 gains lock {opts.g_lock} base {opts.g_base}')
    for s in sizes:
        k = FOCAL * s / (r_raw * 640)
        f, colour, alpha, blended = ns['face_arrays'](src, mats, model, k, dens)
        tot = None
        for axis in [int(x) for x in opts.axes.split(',')]:
            st = stats_lock(f, colour, alpha, blended, axis, k)
            if tot is None:
                tot = st
            else:
                for key in ('px', 'flip_share', 'sd_sum', 'plate_px'):
                    tot[key] += st[key]
                for rule in ('flip', 'ridge', 'both', 'resid', 'resid2', 'flipveto'):
                    for key in st[rule]:
                        tot[rule][key] += st[rule][key]
        px, sdsum = max(tot['px'], 1), max(tot['sd_sum'], 1e-9)
        today = opts.g_lock * tot['sd_sum']
        base = opts.g_base * tot['sd_sum']
        print(f'  s {s:g}: covered px {tot["px"]} flip share {tot["flip_share"] / px:.3f} plate px {tot["plate_px"]} '
              f'({100 * tot["plate_px"] / px:.1f} %); rest ripple today (all 0.985) 1.00, all at base {base / today:.1f}x')
        for rule in ('flip', 'ridge', 'both', 'resid', 'resid2', 'flipveto'):
            r = tot[rule]
            print(f'    {rule:5s}: lock share {r["locked"] / px:.3f} ripple energy captured {r["sd_locked"] / sdsum:.3f} '
                  f'plate locked {100 * r["plate_locked"] / max(tot["plate_px"], 1):.2f} % of plate px '
                  f'refresh>=2 {r["refresh2"] / max(r["locked"], 1):.2f} predicted rest ripple vs today {r["predicted"] / today:.2f}x')
    print(f'  ({time.time() - t0:.0f} s)')
