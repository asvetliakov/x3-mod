#!/usr/bin/env python3
"""The end-on disc's slab profile under the revised look law's detail level (engine_plume_ps.hlsl, the disc at
detail < 1; docs/architecture/engine-exhaust-look-critique.md "Implemented"): one analytic function of rho (nozzle widths)
in place of the previous law's 8-sample integral, a (1 - smoothstep(r0, r1, rho)) (1 + b (1 - smoothstep(0, r2, rho))),
fitted by grid search to the previous law's mean sampled profile at s = 1 (plume_end_on_model.py, still look). Prints
the fit, its worst error over rho in [0, 1] at s = 1 / 0.5 / 0 and the integrated energy ratio. Usage: python3
plume_slab_disc_fit.py; the output is plume_slab_disc_fit_out.txt.
"""
import itertools

import numpy as np

import plume_end_on_model as m


def ss(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def form(rho, a, r0, r1, b, r2):
    return a * (1 - ss(r0, r1, rho)) * (1 + b * (1 - ss(0, r2, rho)))


rho = np.linspace(0, 1.0, 401)
target = m.mean_profile("previous", rho, 1.0)
best = None
for r0, r1, b, r2 in itertools.product(np.arange(0.0, 0.31, 0.02), np.arange(0.36, 0.66, 0.02), np.arange(0.0, 1.01, 0.1),
                                       np.arange(0.1, 0.61, 0.05)):
    if r1 <= r0 + .05:
        continue
    shape = form(rho, 1.0, r0, r1, b, r2)
    a = float((shape * target).sum() / max((shape * shape).sum(), 1e-12))
    err = float(np.abs(a * shape - target).max())
    if best is None or err < best[0]:
        best = (err, a, r0, r1, b, r2)
err, a, r0, r1, b, r2 = best
print(f"fit: a {a:.4f} r0 {r0:.2f} r1 {r1:.2f} b {b:.2f} r2 {r2:.2f}; worst |error| at s 1 {err:.4f} (peak {target.max():.3f})")
w = 2 * np.pi * rho  # the disc's area weight
for s in (1.0, 0.5, 0.0):
    t = m.mean_profile("previous", rho, s)
    f = form(rho, a, r0, r1, b, r2)
    print(f"s {s}: worst |error| {np.abs(f - t).max():.4f}, energy fit / law {(f * w).sum() / (t * w).sum():.4f}")
