#!/usr/bin/env python3
"""The single plume law parameterised by the detail level d = smoothstep(8, 40, projected nozzle px) (the design change
of 2026-10-04, docs/architecture/engine-exhaust-look-critique.md section 6, "One law"): the revised law always, with
- the core radius x k(d) = k0 + (1 - k0) d (the peaked profile's 0.32 and the hot core's 0.45 core both), the edge
  1 - smoothstep(0.55 - 0.10 d, 1, radial): at d 0 a smooth soft profile of the slab law's width and energy;
- the structure x d: the cells' depth, the turbulence, the erosion, the tongues, the rim and tail darkening, the outer
  sheath; the head colour -> the mean tint as d -> 0;
- the outer sheath (the coordinator's outer flame, no gain): A4 m (1 - m), m = smoothstep(0.1, 1.2, radial), inside its
  own edge 1 - smoothstep(0.65, 1.2, radial), x its growth 0.6 + 0.4 smoothstep(0.1, 0.5, u), coloured 0.5 tint^2 / max
  (the darker, more saturated tint), x d;
- the halo's e-fold and fall as before by d.
Prints: k0 (the slab law's side energy at d 0, s 1, L / n 4), the side energy E(d) / the slab law's at s 1 / 0.5 / 0
for d 0..1, the disc's kappa at d 0 and 1 (the side body energy over the sampled profile's, plume_end_on_model.py's
definition; the disc carries the sheath's energy in its body), and the 40 px gates of plume_outer_flame_model.py at d 1.
Usage: python3 plume_one_law_model.py; the output is plume_one_law_model_out.txt.
"""
import numpy as np

import plume_end_on_model as m
import plume_outer_flame_model as of

ss = m.ss
SHOCK, PERIOD, CFADE, CORE, HALO, HB = .5, .16, .6, .45, 1.1, .20
C13 = 1 / (.45 * CORE)
OUTER = dict(A4=5.0, w0=.1, w1=1.2, e0=.65, e1=1.2, lo=.6, ua=.1, ub=.5)


def law(u, radial, s, d, k0, outer=OUTER):
    """(core term, outer term, the white tint's colour factor of the core, of the outer) per I at detail d."""
    uc = np.clip(u, 0, 1)
    k = k0 + (1 - k0) * d
    crest = .5 + .5 * np.cos(2 * np.pi * u / PERIOD)
    env = np.exp(-u * 5 * CFADE) * ss(0, 1, u * 2 / PERIOD) * (1 - ss(.3, .9, radial)) * s * d
    cells = 1 - min(1, 1.7 * SHOCK) * env * (1 - crest ** 3)
    hot = np.exp(-(radial * C13 / k) ** 2)
    boost = 1 + .6 * hot * (1 - .5 * ss(.2, .8, uc))
    edge = 1 - ss(.55 - .10 * d, 1, radial)
    core = edge * cells * (.08 + .92 * np.exp(-(radial / (.32 * k)) ** 2)) * boost
    mw = ss(outer["w0"], outer["w1"], radial)
    grow = outer["lo"] + (1 - outer["lo"]) * ss(outer["ua"], outer["ub"], uc)
    out = d * (1 - ss(outer["e0"], outer["e1"], radial)) * cells * outer["A4"] * mw * (1 - mw) * grow
    dark = 1 - .4 * d * ss(.6, 1, uc)
    tone = 1 + (dark - 1) * ss(.2, .5, uc)
    across = tone + (.5 - tone) * d * ss(.25, .9, radial)
    heat = .7 * hot * (1 - ss(.05, .3, u))
    return core, out, across + (1 - across) * heat, .5 * dark


def slab(u, radial, s):
    """The previous law's slab (the shader's detail-0 branch before the single law), colour 1 on white."""
    crest = .5 + .5 * np.cos(2 * np.pi * u / PERIOD)
    env = np.exp(-u * 5 * CFADE) * ss(0, 1, u * 2 / PERIOD) * (1 - ss(.3, .9, radial)) * s
    return (1 - ss(.55, 1, radial)) * (1 + .6 * (1 - ss(0, 1, radial * C13 * .3214286))) * (1 + SHOCK * env * (2 * crest - 1))


def grid(ln, dx=.01):
    X, Y = np.meshgrid(np.arange(-3, ln + 3, dx), np.arange(-3, 3, dx))
    u, r = X / ln, np.abs(Y)
    uc = np.clip(u, 0, 1)
    return X, u, uc, r, r / m.width(uc), m.tailf(u) * ((u >= 0) & (u <= 1))


def halo(X, uc, r, ln, d, ih):
    sig = (.5 + (.32 - .5) * d) * HALO
    dd = np.hypot(X - np.clip(X, 0, ln), r) / sig
    return ih * np.exp(-dd) * np.exp(-(2.2 + 1.8 * d) * uc) * np.clip(2 * (2.25 - dd), 0, 1)


def energies(s, ln, d, k0):
    """The side frame's total (largest channel, white tint, body + halo): the single law at d, the slab law (halo d 0)."""
    i, ih = 1.2 + 2.8 * s, HB * (1.2 + 2.8 * s) / 4
    X, u, uc, r, radial, tl = grid(ln)
    core, out, cc, oc = law(u, radial, s, d, k0)
    body = i * tl * (core * cc + out * oc)
    e = body.sum() + halo(X, uc, r, ln, d, ih).sum()
    e0 = (i * tl * slab(u, radial, s)).sum() + halo(X, uc, r, ln, 0.0, ih).sum()
    return e / e0, (i * tl * (core + out)).sum() * .01 * .01


def kappa(s, ln, d, k0, dx=.01, sheath=True):
    """The disc's kappa at d: the side body energy (core + sheath, radiance) over L / n x I x the 8-sample mean profile's
    2D integral (the disc's samples: the core and, since the single law, the outer sheath: the end-on annulus)."""
    i = 1.2 + 2.8 * s
    _, side_body = energies(s, ln, d, k0)
    X, Y = np.meshgrid(np.arange(-3, 3, dx), np.arange(-3, 3, dx))
    rho = np.hypot(X, Y)
    terms = [law(np.float64(u), rho / w, s, d, k0) for w, t, u in zip(m.wk, m.tk, m.uk)]
    mean = sum(t * (c + (o if sheath else 0)) for (c, o, _, _), t in zip(terms, m.tk)) / 8
    return side_body / (ln * i * mean.sum() * dx * dx)


def hw_ratio(d, k0, s=1.0):
    """The 10 % half-width at u 0.2 (white, linear, core + sheath) over the slab law's."""
    rs = np.arange(0, 1.6, .005)
    w = float(m.width(.2))
    core, out, cc, oc = law(np.float64(.2), rs / w, s, d, k0)
    v = core * cc + out * oc
    v0 = slab(np.float64(.2), rs / w, s)
    return rs[np.argmax(v < .1 * v[0])] / rs[np.argmax(v0 < .1 * v0[0])]


if __name__ == "__main__":
    # k0: the d-0 law's side energy equal to the slab law's at s 1, L / n 4 (bisection on k0).
    lo, hi = 1.0, 4.0
    for _ in range(30):
        mid = .5 * (lo + hi)
        lo, hi = (mid, hi) if energies(1.0, 4.0, 0.0, mid)[0] < 1 else (lo, mid)
    k0 = round(.5 * (lo + hi), 2)
    print(f"# k0 = {k0} (the core radius x k0 at d 0: the side energy equal to the slab law's at s 1, L / n 4)")
    print("d | " + " | ".join(f"E/slab s {s} L/n {ln}" for s, ln in ((1.0, 4.0), (.5, 2.25), (0.0, 1.0))) + " | hw/slab u 0.2")
    for d in (0.0, .043, .25, .5, .75, .9, 1.0):
        row = [f"{energies(s, ln, d, k0)[0]:.3f}" for s, ln in ((1.0, 4.0), (.5, 2.25), (0.0, 1.0))]
        print(f"{d} | " + " | ".join(row) + f" | {hw_ratio(d, k0):.2f}")
    k_0, k_1 = kappa(1.0, 4.0, 0.0, k0), kappa(1.0, 4.0, 1.0, k0)
    print(f"# disc kappa (energy-matched, s 1, L / n 4; the samples with the sheath): d 0 {k_0:.3f}, d 1 {k_1:.3f} "
          f"(without the sheath in the samples: d 1 {kappa(1.0, 4.0, 1.0, k0, sheath=False):.3f})")
    # The 40 px gates at d 1 (plume_outer_flame_model.py's measures; its split law at gain 1 is this law at d 1).
    p = dict(of.BASE, split=True, gain=1.0, gc=1.0, ga=.25, gb=.5, sat=2.0, outer_grow=True, **OUTER)
    v = of.evaluate(p)
    print("# d 1 gates (model; fixture calibration: axis 0.161 <-> 0.157, red rim 0.49 <-> 0.53): " +
          ", ".join(f"{k} {float(x):.3f}" for k, x in v.items()))
