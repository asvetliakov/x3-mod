# A numpy replica of weapon_fireball_ps.hlsl's fire branch on a face-on sphere (no lane), and of the fixture's fire
# measures (weapon_effects_class_cases.h burst::fire_of; licks against the equal-area disc), for tuning the constants
# against docs/architecture/fire-material.md section 6 and the body erosion gates. K: the shipped look::fire_* constants
# (2026-11-07). It tracked the first native run within about 0.05 on every measure (hull thin 0.28 / 0.32 native, gaps
# 0.095 / 0.079, core 3.42 / 3.25, outside 2.25 / 2.23).
import numpy as np, math

def frac(x): return x - np.floor(x)
def hash2(x, y):
    px, py, pz = frac(x * 0.1031), frac(y * 0.1031), frac(x * 0.1031)
    d = px * (py + 33.33) + py * (pz + 33.33) + pz * (px + 33.33)
    px, py, pz = px + d, py + d, pz + d
    return frac((px + py) * pz)
def vn(x, y):
    ix, iy = np.floor(x), np.floor(y); fx, fy = x - ix, y - iy
    ux, uy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a, b, c, d = hash2(ix, iy), hash2(ix + 1, iy), hash2(ix, iy + 1), hash2(ix + 1, iy + 1)
    return (a + (b - a) * ux) * (1 - uy) + (c + (d - c) * ux) * uy
def vnp(a, s, cells):
    ia, iy = np.floor(a), np.floor(s); fx, fy = a - ia, s - iy
    ix = ia - cells * np.floor(ia / cells); i1 = np.where(ix + 1 > cells - 0.5, 0, ix + 1)
    ux, uy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    A, B, C, D = hash2(ix, iy), hash2(i1, iy), hash2(ix, iy + 1), hash2(i1, iy + 1)
    return (A + (B - A) * ux) * (1 - uy) + (C + (D - C) * ux) * uy
def vn1p(t, cells, seed):
    i = np.floor(t); f = t - i; i0 = i - cells * np.floor(i / cells); i1 = np.where(i0 + 1 > cells - 0.5, 0, i0 + 1)
    s = f * f * (3 - 2 * f)
    return hash2(i0, seed) + (hash2(i1, seed) - hash2(i0, seed)) * s
def ss(a, b, v):
    t = np.clip((v - a) / (b - a), 0, 1); return t * t * (3 - 2 * t)
RAMP = np.array([[0, 0, 0], [0.45, 0.05, 0.015], [1.0, 0.33, 0.05], [1.0, 0.80, 0.30], [1.0, 0.97, 0.85]])
def ramp(T):
    x = np.clip(T, 0, 1) * 4; i = np.minimum(np.floor(x), 3).astype(int); f = (x - i)[..., None]
    return RAMP[i] + (RAMP[i + 1] - RAMP[i]) * f

K = dict(cells=23, s_cells=1.9, warp_f=2.2, warp_amp=0.6, shear=1.0, blend0=0.12, blend1=0.4, flow=120, flick_ms=65,
         flick_amp=0.3, thr0=0.12, thr_t=0.43, thr_r=0.57, erode_rt=3.0, soft=0.10, gain=1.6, gain_small=1.2,
         heat_soft=0.48, rim_t=0.18, rcool=0.63, oct2=0.35, core_r0=0.2, core_r1=0.35, lobes=5, lobe_amp=0.25,
         lobe_soft=0.06)

def render(N, R_px, x, T, E, g, tg, seed, lod, k, fire=1.0, rs_over_r=1.25):
    """N x N frame, a sphere of R_s = rs_over_r R(t) px at the centre; returns linear RGB."""
    Rs = R_px * rs_over_r
    yy, xx = np.mgrid[0:N, 0:N]
    qx = (xx + 0.5 - N / 2) / Rs; qy = -(yy + 0.5 - N / 2) / Rs
    b2 = qx * qx + qy * qy
    inside = b2 < 1
    column = np.where(inside, np.clip(1 - b2, 0, 1) ** 1.5, 0)
    rq2 = b2; rq = np.sqrt(b2); px, py = qx, qy
    rho = (1 + k['lobe_amp'] * (2 * vn1p(k['lobes'] * (np.arctan2(py, px) / (2 * math.pi) + 0.5), k['lobes'], 5.5) - 1)) / (1 + k['lobe_amp'])
    lobe = np.clip((rho - rq) / k['lobe_soft'], 0, 1)
    fq = (1.6, 3.4, 7.0); w = (0.5, 0.3, 0.2); adv = 1.0 * x
    gk = 0.5 / max(adv, 1e-4); pp = rq2 * gk * gk
    g0, g1, g2 = [0.5 * f / np.sqrt(1 + pp * f * f) for f in fq]
    o = seed
    wpx = min(lod, 1.0)
    wt = fire * wpx * ss(k['blend0'], k['blend1'], rq)
    n = w[0] * vn(px * (fq[0] - g0) + o * 1.7, py * (fq[0] - g0) + o * 3.1) + w[1] * vn(px * (fq[1] - g1) + o * 3.1 + 5.3, py * (fq[1] - g1) + o * 1.7 + 5.3)
    w3 = w[2] * (1 - wt)
    n = (n + w3 * vn(px * (fq[2] - g2) + o * 0.7 + 11.1, py * (fq[2] - g2) + o * 2.3 + 11.1)) / (w[0] + w[1] + w3)
    wa = k['warp_amp'] * (2 * vn(px * k['warp_f'] + o * 0.9 + 21.3, py * k['warp_f'] + o * 2.7 + 21.3) - 1)
    wb = k['warp_amp'] * (2 * vn(px * k['warp_f'] + o * 2.1 + 31.7, py * k['warp_f'] + o * 0.5 + 31.7) - 1)
    u = np.arctan2(py, px) / (2 * math.pi) + 0.5; C = k['cells']
    a = C * u + k['shear'] * rq * wb + 1.3 * o
    s = rq * k['s_cells'] - tg / k['flow'] + wa + 0.7 * o
    wo = k['oct2']
    nt = (1 - wo) * vnp(a, s, C) + wo * vnp(2 * a + 0.5, 2 * s + 3.7, 2 * C)
    n = n + (nt - n) * wt
    fl = 1 - k['flick_amp'] + k['flick_amp'] * vn1p(C * u + tg / k['flick_ms'], C, o)
    flick = 1 + (fl - 1) * wt
    thr = k['thr0'] + k['thr_t'] * x + k['thr_r'] * rq2 * (1 + k['erode_rt'] * x)
    d = np.clip((n - thr) / k['soft'], 0, 1)
    gain = k['gain_small'] + (k['gain'] - k['gain_small']) * wpx
    dens = np.where(d > 1e-6, d ** gain, 0) * flick * column ** 0.25 * lobe
    m = np.clip((n - thr) / k['heat_soft'], 0, 1)
    Tp = T * (k['rim_t'] + (1 - k['rim_t']) * m ** 0.75) * (1 - k.get('rcool', 0) * ss(0.45, 0.95, rq))
    gc = g * np.clip((k['core_r1'] - rq) / (k['core_r1'] - k['core_r0']), 0, 1)
    col = ramp(Tp)
    col = col + (1 - col) * np.minimum(1, gc)[..., None]
    out = col * (E * dens * (1 + gc))[..., None]
    out[~inside] = 0
    return out

def fill_holes(m):
    pad = np.zeros((m.shape[0] + 2, m.shape[1] + 2), bool); pad[1:-1, 1:-1] = m
    reach = np.zeros_like(pad); reach[0, :] = reach[-1, :] = reach[:, 0] = reach[:, -1] = True
    free = ~pad
    while True:
        grow = reach.copy()
        grow[1:, :] |= reach[:-1, :]; grow[:-1, :] |= reach[1:, :]; grow[:, 1:] |= reach[:, :-1]; grow[:, :-1] |= reach[:, 1:]
        grow &= free; grow |= reach
        if (grow == reach).all(): break
        reach = grow
    return pad[1:-1, 1:-1] | ~(reach[1:-1, 1:-1] | pad[1:-1, 1:-1])
def erode(m, it):
    for _ in range(it):
        e = m.copy()
        e[1:, :] &= m[:-1, :]; e[:-1, :] &= m[1:, :]; e[:, 1:] &= m[:, :-1]; e[:, :-1] &= m[:, 1:]
        m = e
    return m
def box(l, r):
    c = np.pad(l, ((1, 0), (1, 0))).cumsum(0).cumsum(1); N, M = l.shape
    y0 = np.clip(np.arange(N) - r, 0, N); y1 = np.clip(np.arange(N) + r + 1, 0, N)
    x0 = np.clip(np.arange(M) - r, 0, M); x1 = np.clip(np.arange(M) + r + 1, 0, M)
    s = c[y1][:, x1] - c[y0][:, x1] - c[y1][:, x0] + c[y0][:, x0]
    return s / ((y1 - y0)[:, None] * (x1 - x0)[None, :])
STOPS = RAMP[1:] / RAMP[1:].max(axis=1, keepdims=True)
def measures(rgb, R):
    N = rgb.shape[0]; c = N / 2
    l = rgb.max(axis=2); pk = l.max()
    if pk <= 0: return {}
    lit = l > 0.1 * pk
    filled = fill_holes(lit)
    yy, xx = np.mgrid[0:N, 0:N]; r = np.hypot(xx + 0.5 - c, yy + 0.5 - c) / R
    n_lit = lit.sum()
    thin = (l[lit] > 0.8 * pk).mean()
    gaps = (l[filled] < 0.25 * pk).mean()
    def edges(m):
        e = 0
        for ax in (0, 1):
            for sh in (1, -1):
                e += (m & ~np.roll(m, sh, axis=ax)).sum()
        return e
    licks_fill = edges(lit) / max(edges(filled), 1)
    disc_ref = 8 * math.sqrt(filled.sum() / math.pi)
    licks_disc = edges(lit) / disc_ref
    inner = erode(filled, 4)
    lo = box(l, max(1, int(R / 4 + 0.5)))
    hp = l - lo
    gy, gx = np.gradient(hp)
    dx, dy = xx + 0.5 - c, yy + 0.5 - c; rr = np.maximum(np.hypot(dx, dy), 1e-6)
    gr = (gx * dx + gy * dy) / rr; gt = (-gx * dy + gy * dx) / rr
    aniso = (gr[inner] ** 2).sum() / max((gt[inner] ** 2).sum(), 1e-12)
    cn = rgb[lit] / np.maximum(rgb[lit].max(axis=1, keepdims=True), 1e-9)
    cls = np.argmin(((cn[:, None, :] - STOPS[None]) ** 2).sum(axis=2), axis=1)  # 0 red .25, 1 mid .5, 2 .75, 3 1.0
    rl = r[lit]
    hot = (cls >= 2).mean(); cold = (cls == 0).mean(); mid = (cls == 1).mean()
    hot_r = rl[cls >= 2].mean() if (cls >= 2).any() else 0; cold_r = rl[cls == 0].mean() if (cls == 0).any() else 0
    share_in = (l[r <= 1] > 0.1 * pk).mean()
    return dict(thin=thin, gaps=gaps, licks_fill=licks_fill, licks_disc=licks_disc, aniso=aniso, hot=hot, hot_r=hot_r,
                cold=cold, cold_r=cold_r, mid=mid, core=l[r <= 0.3].max(), out=l[r > 0.5].max(), lit_in=share_in)
