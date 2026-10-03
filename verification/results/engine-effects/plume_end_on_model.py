#!/usr/bin/env python3
"""After flight C: the numeric model behind the end-on disc's constants (src/proxy/engine_plumes_core.h Look:
disc_kappa 1.8, disc_halo 3, disc_cap 1.5, the facing band 0.3..0.7, axial_floor 0.5; docs/architecture/
engine-effects-modern.md "After flight C"). The still look (no turbulence, erosion or pulse), lengths in nozzle widths.

kappa: the side view's body energy over the 8-sample integrated end-on profile's, per nozzle width of length
(I x L / n). Energy: the end-on disc's (soft-capped body + halo) against the side view's (body + halo) at s 0 / 0.5 / 1
and L / n 0.5..8 (the default nozzle 0.5 gives L / n = 2 z: 0.5, 2.25, 4). The facing rows add the axial quad's
foreshortened share, approximated by sqrt(1 - f^2) without the mouth hand-over (the fixture's END_ON rows measure the
drawn frame). Prints one row per case; the output is plume_end_on_model_out.txt.
"""
import numpy as np

bulge, taper, tail, shock, period, cfade, core, halo, hb = 1.15, .45, .7, .5, .16, .6, .45, 1.1, .35
KAPPA, HALO, CAP = 1.8, 3.0, 1.5
c0, c1, c2, c3 = .5 * bulge, (.04 - .5 * bulge) * taper, .5 * bulge * taper, .6 * taper
tf, t12 = .75 + (.25 - .75) * tail, 1.2 * tail


def ss(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def width(u):
    b = c0 * (1 - .55 * np.exp(-9 * u)) * (1 + .35 * ss(0, .25, u) * np.exp(-4 * u))
    return np.minimum(c0 + c1 * u, b + c2) * np.maximum(1 - c3 * ss(.6, 1, u), .05)


def tailf(u):
    return (1 - ss(tf, 1, u)) * np.exp(-u * t12)


def cell(u):
    return shock * np.cos(2 * np.pi * u / period) * np.exp(-u * 5 * cfade) * ss(0, period, u)


def profile(radial, s, c):
    edge, cm = 1 - ss(.55, 1, radial), 1 - ss(0, 1, radial / (1.4 * core))
    return edge * (1 + c * (1 - ss(0, .8, radial)) * s) * (1 + .6 * cm)


def side(s, ln, dx=.01):
    i, ih, sig = 1.2 + 2.8 * s, hb * (.3 + .7 * s), .5 * halo
    X, Y = np.meshgrid(np.arange(-3, ln + 3, dx), np.arange(-3, 3, dx))
    u, r = X / ln, np.abs(Y)
    uc = np.clip(u, 0, 1)
    body = i * tailf(u) * ((u >= 0) & (u <= 1)) * profile(r / width(uc), s, cell(uc))
    d = np.hypot(X - np.clip(X, 0, ln), r) / sig
    h = ih * np.exp(-d) * np.exp(-2.2 * uc) * np.clip(2 * (2.25 - d), 0, 1)
    total = body + h
    mouth = total[:, X[0] <= .15 * ln].max()
    crest = total[:, (X[0] >= .1 * ln) & (X[0] <= .4 * ln)].max()
    return body.sum() * dx * dx, total.sum() * dx * dx, mouth, crest


uk = (np.arange(8) + .5) / 8
wk, tk, ck = width(uk), tailf(uk), cell(uk)


def disc(s, ln, f=1.0, kappa=KAPPA, dx=.01):
    i, ih, sig = 1.2 + 2.8 * s, hb * (.3 + .7 * s), .5 * halo
    X, Y = np.meshgrid(np.arange(-3, 3, dx), np.arange(-3, 3, dx))
    rho = np.hypot(X, Y)
    mean = sum(t * profile(rho / w, s, c) for w, t, c in zip(wk, tk, ck)) / 8
    crest = float(tailf(period)) * (1 + s * shock * np.exp(-5 * cfade * period))
    cap = CAP * i * 1.6 * max(1.0, crest)
    x = i * kappa * ln * f * mean + ih * HALO * ln * f * np.exp(-rho / sig) * np.clip(2 * (2.25 - rho / sig), 0, 1)
    shown = cap * (1 - np.exp(-x / cap))
    return shown.sum() * dx * dx, shown.max(), mean.sum() * dx * dx


if __name__ == '__main__':
    for s, ln in ((1, 4), (.5, 2.25), (0, .5), (1, 8), (.5, 4.5), (1, 2)):
        eb, e, mouth, crest = side(s, ln)
        kappa = eb / ((1.2 + 2.8 * s) * ln * disc(s, ln)[2])
        ed, pk, _ = disc(s, ln)
        row = [f's={s} L/n={ln} kappa={kappa:.3f} side_E={e:.3f} mouth/crest={mouth / crest:.3f} end_on_E/side_E={ed / e:.3f} '
               f'end_on_peak/crest={pk / crest:.3f} facing']
        for f in (1.0, .866, .7, .5, .3, 0.0):
            wd = float(ss(.3, .7, f))
            share = (1 - .5 * wd) * np.sqrt(1 - f * f) + (wd * disc(s, ln, f)[0] / e if wd > 0 else 0.0)
            row.append(f'{f}:{share:.2f}')
        print(' '.join(row))
