#!/usr/bin/env python3
"""fire_red_band.py [law ...]: the round-3 turbulence law's colour classes (fire-material.md 13.3, the red band) on a
numpy replica of weapon_fireball_ps.hlsl's fire branch, face-on, against fire_colour_ramp_{hull,death}'s per-pixel
classes (weapon_effects_class_cases.h fire_of: the linear max channel's lit set > 0.1 of its peak in the 1.6 R box, the
nearest look::fire_ramp stop, hot T >= 0.75, cold T <= 0.3, mid between; cold_r / hot_r their mean radius over R) and
the AgX pink share (agx_display: B > 0.5 G and G < 0.85 R, under 0.8 of the peak). Value noise from a seeded lattice
(not the program's hash): the classes depend on the ridged sum's distribution, not its cells. Laws: 'old' (T' = T (rim_t
+ (1 - rim_t) saturate(F / heat_soft)^0.75), lum lerp(lum0, 1, m); the round-3 record's with old:lum0=0.6) and 'band' (the shipped round-3 red band: tau 0.25 ->
red_top over [cut, cut + red_w], then -> 1 at the densest top_share of [cut, F_top]; the luminance lerp(lum0, 1, m) as
before, raised to 1 at the band's outer edge, 1 - (1 - lum) h_band). Medians over
8 seeds; the hull at 0.2 L_b (core 2 (1 - 0.2 / 0.5)^2), the death at 0.2 L (core 5 (1 - 0.2 / 0.35)^2, T 0.825)."""
import sys
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools/analysis'))
import agx_reference as A  # noqa: E402

RAMP = np.array([[0, 0, 0], [0.47, 0.15, 0], [0.62, 0.34, 0], [1.0, 0.78, 0.06], [1.0, 0.92, 0.3]])
K = dict(raise_=1.0, k=0.75, c=0.25, thr0=0.12, thrt=0.05, thrr=0.62, thrrt=0.3, soft=0.15, gain=1.2, heat_soft=0.5, rim_t=0.21,
         lum0=0.7, lumd=0.5, a=0.55, env=0.2, cut=0.035, red_w=0.15, red_top=0.55, top_share=0.5)


def ramp(t):
    x = np.clip(t, 0, 1) * 4
    i = np.minimum(np.floor(x), 3).astype(int)
    f = (x - i)[..., None]
    return RAMP[i] * (1 - f) + RAMP[i + 1] * f


def vnoise(rng, cells, X, Y):
    lat = rng.random((int(cells * 4) + 4, int(cells * 4) + 4))
    x, y = X * cells + 2 * cells, Y * cells + 2 * cells
    ix, iy = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = x - ix, y - iy
    ux, uy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a, b, c, d = lat[iy, ix], lat[iy, ix + 1], lat[iy + 1, ix], lat[iy + 1, ix + 1]
    return (a + (b - a) * ux) * (1 - uy) + (c + (d - c) * ux) * uy


def agx_pink(rgb):
    v = np.maximum(rgb, 0) ** A.DECODE_GAMMA @ np.array(A.M_IN).T
    e = np.clip(np.log2(np.maximum(v, A.LOG_FLOOR)), A.MIN_EV, A.MAX_EV)
    x = (e - A.MIN_EV) / (A.MAX_EV - A.MIN_EV)
    c = np.polyval(A.CONTRAST_COEFFICIENTS, x)
    o = np.clip(c @ np.array(A.M_OUT).T, 0, 1)
    return (o[..., 2] > 0.5 * o[..., 1]) & (o[..., 1] < 0.85 * o[..., 0])


def render(law, R, rs, x, Tt, g0, seed, kk=K):
    n = int(3.4 * R)
    yy, xx = np.mgrid[0:n, 0:n] + 0.5 - n / 2
    Rs = rs * R
    X, Y = xx / Rs, yy / Rs
    rp = np.hypot(X, Y)
    rng = np.random.default_rng(seed)
    lw = Rs / (3 * 2)
    wp = np.clip((rp - 0.15) / 0.2, 0, 1)
    wp = wp * wp * (3 - 2 * wp)
    w = [np.clip(lw - 1, 0, 1) * (0.25 + 0.75 * wp), np.clip(lw / 2 - 1, 0, 1) * wp, np.clip(lw / 4 - 1, 0, 1) * wp,
         np.clip(lw / 8 - 1, 0, 1) + 0 * wp, np.clip(lw / 16 - 1, 0, 1) + 0 * wp]
    a = kk['a']
    nz = [vnoise(rng, 3 * 2 ** i, X, Y) for i in range(5)]
    amp = [w[i] * a ** i for i in range(5)]
    T = sum(amp[i] * np.abs(2 * nz[i] - 1) for i in range(5)) / np.maximum(sum(amp), 1e-4)
    wf = amp[2] + amp[3] + amp[4]
    tfn = (amp[2] * np.abs(2 * nz[2] - 1) + amp[3] * np.abs(2 * nz[3] - 1) + amp[4] * np.abs(2 * nz[4] - 1)) / np.maximum(wf, 1e-4)
    re = rp / (1 + kk['env'] * (2 * nz[0] - 1))
    F = 0.5 + kk['k'] * (kk['c'] - T) - (kk['thr0'] + kk['thrt'] * x + kk['thrr'] * re * re * (1 + kk['thrrt'] * x))
    sF = np.clip(F / kk['soft'], 0, 1)
    al = sF * sF * (3 - 2 * sF)
    column = np.clip(1 - rp * rp, 0, 1) ** 1.5
    lobe = np.clip((1 - rp) / 0.06, 0, 1)
    mf = np.clip(F / kk['heat_soft'], 0, 1)
    if law == 'old':
        lum = kk['lum0'] + (1 - kk['lum0']) * mf
        tau = kk['rim_t'] + (1 - kk['rim_t']) * mf ** 0.75
    else:
        ftop = 0.5 + kk['k'] * kk['c'] - kk['thr0']
        cut, rw = kk['cut'], kk['red_w']
        hb = np.clip((F - cut) / rw, 0, 1)
        fw = ftop - kk['top_share'] * (ftop - cut)
        hu = np.clip((F - cut - rw) / max(fw - cut - rw, 1e-3), 0, 1)
        tau = 0.25 + (kk['red_top'] - 0.25) * hb + (1 - kk['red_top']) * hu
        lum_d = kk['lum0'] + (1 - kk['lum0']) * mf
        lum = np.where(F < cut + rw, lum_d + kk['raise_'] * (1 - lum_d) * (1 - hb), lum_d)
    dens = al ** kk['gain'] * (al > 1e-6) * column ** 0.25 * lobe * lum * (1 - kk['lumd'] * tfn)
    g = g0 * np.clip((0.35 - rp) / 0.15, 0, 1)
    c = ramp(Tt * tau)
    c = c + (RAMP[4] - c) * np.clip(g, 0, 1)[..., None]
    rgb = c * (1.5 * dens * (1 + g))[..., None]
    return rgb, np.hypot(xx, yy) / R


def classes(rgb, r):
    box = r <= 1.6
    l = rgb.max(-1)
    pk = l[box].max()
    lit = box & (l > 0.1 * pk)
    px = rgb[lit] / np.maximum(l[lit], 1e-9)[:, None]
    stops = RAMP[1:] / RAMP[1:].max(1, keepdims=True)
    T = 0.25 * (1 + np.argmin(((px[:, None, :] - stops[None]) ** 2).sum(-1), 1))
    rr = r[lit]
    hot, cold = T >= 0.75, T <= 0.3
    pink = agx_pink(rgb[lit]) & (l[lit] <= 0.8 * pk)
    return dict(hot=hot.mean(), hot_r=rr[hot].mean() if hot.any() else 0, cold=cold.mean(),
                cold_r=rr[cold].mean() if cold.any() else 0, mid=1 - hot.mean() - cold.mean(), pink=pink.mean())


def agx_luma(rgb):
    v = np.maximum(rgb, 0) ** A.DECODE_GAMMA @ np.array(A.M_IN).T
    e = np.clip(np.log2(np.maximum(v, A.LOG_FLOOR)), A.MIN_EV, A.MAX_EV)
    c = np.polyval(A.CONTRAST_COEFFICIENTS, (e - A.MIN_EV) / (A.MAX_EV - A.MIN_EV))
    o = np.clip(c @ np.array(A.M_OUT).T, 0, 1)
    return o @ np.array(A.LUMA_WEIGHTS)


def core_share(rgb, r, R):
    """turbulence_of's core: the 4-connected region >= 0.5 of the AgX luma's peak holding the centre over the
    equal-area disc of the lit set (> 0.1 of the peak); and its hf, the 4-neighbour Laplacian's energy over the luma's
    inside that disc."""
    box = r <= 1.6
    l = np.where(box, agx_luma(rgb), 0)
    pk = l.max()
    req = np.sqrt((l > 0.1 * pk).sum() / np.pi)
    nd = (r * R <= req).sum()
    m = l >= 0.5 * pk
    reg = np.zeros_like(m)
    reg[l.shape[0] // 2, l.shape[1] // 2] = m[l.shape[0] // 2, l.shape[1] // 2]
    while True:  # 4-connected flood fill from the centre
        g = reg | np.roll(reg, 1, 0) | np.roll(reg, -1, 0) | np.roll(reg, 1, 1) | np.roll(reg, -1, 1)
        g &= m
        if (g == reg).all():
            break
        reg = g
    lap = 4 * l - np.roll(l, 1, 0) - np.roll(l, -1, 0) - np.roll(l, 1, 1) - np.roll(l, -1, 1)
    ins = r * R <= req
    return reg.sum() / nd, float((lap[ins] ** 2).sum() / max((l[ins] ** 2).sum(), 1e-12))


def run(law, kk=K, seeds=range(8)):
    out = {}
    for name, R, rs, x, Tt, g0 in (('hull', 60, 1.25, 0.3, 0.7, 2 * (1 - 0.3 / 0.5) ** 2),
                                    ('death', 44, 1.33, 0.3, 1 - 0.7 * 0.3 / 0.8, 5 * (1 - 0.3 / 0.35) ** 2)):
        cs = [core_share(*render(law, R, rs, x, Tt, g0, 1000 + s, kk), R) for s in seeds]
        out[name + '_core3'] = float(np.median([c[0] for c in cs]))
        out[name + '_hf3'] = float(np.median([c[1] for c in cs]))
    for name, R, rs, x, Tt, g0 in (('hull', 60, 1.25, 0.2, 0.8, 2 * (1 - 0.2 / 0.5) ** 2),
                                    ('death', 44, 1.33, 0.2, 0.825, 5 * (1 - 0.2 / 0.35) ** 2)):
        ms = [classes(*render(law, R, rs, x, Tt, g0, 1000 + s, kk)) for s in seeds]
        out[name] = {k: float(np.median([m[k] for m in ms])) for k in ms[0]}
        out[name]['core3'] = out.pop(name + '_core3')
        out[name]['hf3'] = out.pop(name + '_hf3')
    return out


if __name__ == '__main__':
    laws = sys.argv[1:] or ['old', 'band']
    for law in laws:
        kk = dict(K)
        if ':' in law:
            law, rest = law.split(':', 1)
            for kv in rest.split(','):
                k, v = kv.split('=')
                kk[k] = float(v)
        for name, m in run(law, kk).items():
            print(law, name, ' '.join(f'{k}={v:.3f}' for k, v in m.items()),
                  'bounds: hot>=0.25 hot_r<=0.5 cold 0.05-0.25 cold_r>=0.65 mid<=0.65 pink<0.02')
