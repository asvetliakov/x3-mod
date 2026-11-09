#!/usr/bin/env python3
"""The muzzle re-shade's per-draw overlap normaliser (docs/architecture/muzzle-flash-look.md section 2; review F1,
2026-11-09), in two parts.

1. The ray sum: body 445's summed glow at the gun (muzzle_inputs.py's overlap metric: every quad a ray through the gun
   crosses adds g = (1 - (t / (0.5 r))^2)^2 at its crossing distance t, r the 90 % lit radius, the default mz_size 0.5)
   against the view ray's angle to the fan's model +z axis (the flash axis, g_mWorld's z column at the draw). The first
   line reproduces the review's 500 directions (mean 9.14, min 4.88, max 27.72); the bins show its 5.9 side-on to 27
   axis-on, and a polynomial fit of it (not used: part 2).
2. --rendered DUMP_OUT: the rendered real mesh, the dump's DUMP_MUZZLE_SWEEP rows (run_weapon_effects.py --dump-case
   muzzle; yaw 0..90 degrees, 30 m, the scale peak). The rendered overlap is proportional to the re-shaded energy times
   the N the draw used; scaled to 9.14 at the 25-degree calibration pose, its linear fit in cos^2 is the
   muzzle_reshade_core.h bodies[0].overlap the header evaluates per draw (no square root: cos^2 = (v . z)^2 / (|v|^2
   |z|^2)).

  python3 -I verification/results/muzzle-reshade/overlap_by_angle.py [--game /mnt/c/X3FL] [--directions 20000] \
    [--rendered verification/results/muzzle-reshade/dump_out.txt] > verification/results/muzzle-reshade/overlap_by_angle_out.txt
Reads body 445 from the FL catalogues (muzzle_inputs.body445); prints numbers only.
"""
import argparse
import contextlib
import io
import pathlib
import re
import sys

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import muzzle_inputs as mi  # noqa: E402

LIT_RADIUS = 45618.0  # the 90 % energy radius (muzzle_inputs_out.txt; muzzle_reshade_core.h bodies[0].lit_radius)
DEGREE = 4            # the polynomial in cos^2 the header evaluates (Horner, four multiply-adds)


def glows(tris, directions, radius=0.5 * LIT_RADIUS):
    """The summed glow per direction (rows of unit vectors): Moller-Trumbore against every triangle of the fan; a ray is a
    full line through the gun (|t|), so d and -d give the same sum."""
    t3 = np.array([[v[0] for v in tri] for tri in tris], float)
    a, e1, e2 = t3[:, 0], t3[:, 1] - t3[:, 0], t3[:, 2] - t3[:, 0]
    out = np.empty(len(directions))
    for k, d in enumerate(directions):
        h = np.cross(d, e2)
        det = np.einsum('ij,ij->i', e1, h)
        ok = np.abs(det) > 1e-9
        f = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
        s = -a
        u = f * np.einsum('ij,ij->i', s, h)
        q = np.cross(s, e1)
        v = f * (q @ d)
        t = f * np.einsum('ij,ij->i', e2, q)
        hit = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (np.abs(t) < radius)
        out[k] = np.where(hit, (1 - (t / radius) ** 2) ** 2, 0.0).sum()
    return out


def fibonacci(n):
    i = np.arange(n)
    z = 1 - 2 * (i + 0.5) / n
    r = np.sqrt(1 - z * z)
    phi = i * np.pi * (3 - np.sqrt(5))
    return np.stack([r * np.cos(phi), r * np.sin(phi), z], 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', default='/mnt/c/X3FL')
    ap.add_argument('--directions', type=int, default=20000)
    ap.add_argument('--rendered', help="the muzzle dump's dump_out.txt (DUMP_MUZZLE_SWEEP rows)")
    args = ap.parse_args()
    with contextlib.redirect_stdout(io.StringIO()):
        _, tris = mi.body445(args.game)
    g500 = glows(tris, fibonacci(500))
    print(f'review lattice (500): mean {g500.mean():.2f} min {g500.min():.2f} max {g500.max():.2f}')
    d = fibonacci(args.directions)
    c = np.abs(d[:, 2])
    g = glows(tris, d)
    print(f'lattice ({args.directions}): mean {g.mean():.3f} min {g.min():.2f} max {g.max():.2f}')
    edges = np.linspace(0, 1, 41)
    cm, c2m, gm = [], [], []
    print('|cos| bin: n, glow mean, min, max')
    for lo, hi in zip(edges[:-1], edges[1:]):
        m = (c >= lo) & (c < hi + 1e-12)
        cm.append(c[m].mean())
        c2m.append((c[m] ** 2).mean())
        gm.append(g[m].mean())
        print(f'  {lo:.3f}-{hi:.3f} {m.sum():5d} {g[m].mean():6.2f} {g[m].min():6.2f} {g[m].max():6.2f}')
    c2m, gm = np.array(c2m), np.array(gm)
    coef = np.polyfit(c2m, gm, DEGREE, w=1 / gm)[::-1]  # weighted: relative error
    fit = lambda x: np.polyval(coef[::-1], x)  # noqa: E731
    rel = fit(c2m) / gm - 1
    per = g / fit(c ** 2)
    print('ray-sum fit N(x) = ' + ' + '.join(f'{k:.4f} x^{i}' for i, k in enumerate(coef)) + '  (x = cos^2; not used)')
    print(f'fit against the bin means: worst {np.abs(rel).max():.3f} rms {np.sqrt((rel ** 2).mean()):.3f}; '
          f'per direction glow / fit: p5 {np.percentile(per, 5):.2f} p95 {np.percentile(per, 95):.2f} '
          f'min {per.min():.2f} max {per.max():.2f} mean {per.mean():.3f} (the azimuth spread |cos| cannot see)')
    print(f'fit mean over the sphere {fit(c ** 2).mean():.3f}; N at side-on (cos 0) {fit(0.0):.2f}, '
          f'25 deg out of the screen plane (cos 0.423) {fit(0.423 ** 2):.2f}, axis-on (cos 1) {fit(1.0):.2f}')
    if args.rendered:
        rendered(args.rendered)


def rendered(path, reference=9.14, reference_yaw=25.0):
    rows = [dict(re.findall(r'(\w+)=(\S+)', line)) for line in open(path) if line.startswith('DUMP_MUZZLE_SWEEP ')]
    if not rows:
        raise SystemExit(f'{path}: no DUMP_MUZZLE_SWEEP rows')
    yaw = np.array([float(r['yaw_deg']) for r in rows])
    c2 = np.array([float(r['c2']) for r in rows])
    e = np.array([float(r['energy_ratio']) * float(r['overlap']) for r in rows])  # proportional to the rendered overlap
    n = reference * e / e[np.argmin(np.abs(yaw - reference_yaw))]
    coef = np.polyfit(c2, n, 1)[::-1]
    rel = np.polyval(coef[::-1], c2) / n - 1
    print(f'rendered sweep ({path}): yaw, cos^2, N used, energy ratio, peak, white r px (vanilla), rendered N')
    for r, x in zip(rows, n):
        print(f"  {float(r['yaw_deg']):4.0f} {float(r['c2']):.4f} {float(r['overlap']):6.3f} {float(r['energy_ratio']):.4f} "
              f"{float(r['reshade_peak']):.3f} {float(r['white_r']):5.1f} ({float(r['vanilla_white_r']):5.1f}) {x:6.3f}")
    print(f'rendered fit N(x) = {coef[0]:.4f} + {coef[1]:.4f} x  (x = cos^2; 9.14 at {reference_yaw:g} deg); '
          f'worst {np.abs(rel).max():.3f}; header coefficients: {{{coef[0]:.4f}f, {coef[1]:.4f}f}}')


if __name__ == '__main__':
    main()
