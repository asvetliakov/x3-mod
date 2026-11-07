import numpy as np, sys, math
# the shader's body law (weapon_fireball_ps.hlsl) in R_s units, a front view, the lane at the hit plane
def frac(x): return x - np.floor(x)
def hash2(qx, qy):
    px, py, pz = frac(qx * 0.1031), frac(qy * 0.1031), frac(qx * 0.1031)
    d = px * (py + 33.33) + py * (pz + 33.33) + pz * (px + 33.33)
    px, py, pz = px + d, py + d, pz + d
    return frac((px + py) * pz)
def vn(x, y):
    ix, iy = np.floor(x), np.floor(y); fx, fy = x - ix, y - iy
    ux, uy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a, b, c, d = hash2(ix, iy), hash2(ix + 1, iy), hash2(ix, iy + 1), hash2(ix + 1, iy + 1)
    return (a + (b - a) * ux) * (1 - uy) + (c + (d - c) * ux) * uy
RAMP = np.array([[0, 0, 0], [0.55, 0.10, 0.03], [1.0, 0.42, 0.10], [1.0, 0.72, 0.30], [1.0, 0.92, 0.80]])
def heat(t):
    x = np.clip(t, 0, 1) * 4; i = np.minimum(np.floor(x), 3).astype(int); f = x - i
    return RAMP[i] * (1 - f[..., None]) + RAMP[i + 1] * f[..., None]
P = dict(freq=(1.6, 3.4, 7.0), w=(0.5, 0.3, 0.2), adv=1.5, e0=0.25, et=0.30, er=0.20, soft=0.12, cpow=0.35, lobes=5, amp=0.25,
         lsoft=0.06, hsoft=0.35, rim=0.45, ert=0.0)
def image(x, seed, P, N=220, oct=3, rise=0.15, r_over_Rb=0.5):
    rng = np.random.default_rng(seed)
    o, th, ls = 64 * rng.random(), 2 * math.pi * rng.random(), 64 * rng.random()
    Rt = 1 - math.exp(-x / rise)  # R(t) / R_b
    Rs = Rt * (1 + P['amp'])
    b = 0.5 * r_over_Rb / Rs  # the bias in R_s units: the lane b behind the centre
    g = np.linspace(-1.05, 1.05, N)
    X, Y = np.meshgrid(g, -g)
    rho2 = X * X + Y * Y
    inside = rho2 < 1
    h = np.sqrt(np.clip(1 - rho2, 0, None))
    u0, u1 = -h, np.minimum(h, b)
    disc = 1 - rho2
    col = np.clip(((u1 - u0) * disc - (u1 ** 3 - u0 ** 3) / 3) * 0.75, 0, 1)
    zq = 0.5 * (u0 + u1)
    rq2 = rho2 + zq * zq
    c, s = math.cos(th), math.sin(th)
    px, py = c * X - s * Y, s * X + c * Y
    pl = np.maximum(np.hypot(px, py), 1e-4)
    a = P['adv'] * x
    dx, dy = px / pl * a, py / pl * a
    f, w = P['freq'], P['w']
    n = w[0] * vn(px * f[0] - dx + o * 1.7, py * f[0] - dy + o * 3.1) + w[1] * vn(px * f[1] - dx + o * 3.1 + 5.3, py * f[1] - dy + o * 1.7 + 5.3)
    ws = w[0] + w[1]
    if oct > 2:
        n += w[2] * vn(px * f[2] - dx + o * 0.7 + 11.1, py * f[2] - dy + o * 2.3 + 11.1); ws += w[2]
    n /= ws
    thr = P['e0'] + P['et'] * x + P['er'] * rq2 * (1 + P['ert'] * x)
    d = np.clip((n - thr) / P['soft'], 0, 1)
    t = P['lobes'] * (np.arctan2(py, px) / (2 * math.pi) + 0.5)
    i0 = np.minimum(np.floor(t), P['lobes'] - 1); fl = np.clip(t - i0, 0, 1)
    i1 = np.where(i0 + 1 > P['lobes'] - 0.5, 0, i0 + 1)
    nl = hash2(i0, ls) + (hash2(i1, ls) - hash2(i0, ls)) * fl * fl * (3 - 2 * fl)
    rho = (1 + P['amp'] * (2 * nl - 1)) / (1 + P['amp'])
    lobe = np.clip((rho - np.sqrt(rq2)) / P['lsoft'], 0, 1)
    dens = d * np.power(np.maximum(col, 1e-6), P['cpow']) * lobe * inside
    m = col * np.clip((n - thr) / P['hsoft'], 0, 1)
    T = (1 - x) * (P['rim'] + (1 - P['rim']) * m)
    rgb = heat(T) * 1.5 * dens[..., None]
    r_over_R = np.sqrt(rho2) * (1 + P['amp'])  # radius over R(t)
    return rgb, r_over_R
def gates(P, seeds=range(12)):
    out = {k: [] for k in ('share01', 'share05', 'share09', 'mr02', 'mr08', 'dark03', 'core', 'rim')}
    for sd in seeds:
        for x, key in ((0.1, 'share01'), (0.5, 'share05'), (0.9, 'share09')):
            rgb, rr = image(x, sd, P); l = rgb.max(-1); pk = l.max()
            inR = rr <= 1
            out[key].append(((l > 0.1 * pk) & inR).sum() / inR.sum())
        for x, key in ((0.2, 'mr02'), (0.8, 'mr08')):
            rgb, rr = image(x, sd, P); l = rgb.max(-1); pk = l.max(); lit = l > 0.1 * pk
            out[key].append(rr[lit].mean())
        rgb, rr = image(0.3, sd, P); l = rgb.max(-1); inR = rr <= 1
        out['dark03'].append(((l < 0.1 * l.max()) & inR).sum() / inR.sum())
        rgb, rr = image(0.2, sd, P)
        for key, sel in (('core', rr <= 0.3), ('rim', (rr >= 0.7) & (rr <= 1))):
            cm = rgb[sel].sum(0); cm = cm / max(cm.max(), 1e-9)
            st = RAMP[1:] / RAMP[1:].max(1, keepdims=True)
            out[key].append(0.25 * (1 + np.argmin(((st - cm) ** 2).sum(1))))
    r = {k: (np.min(v), np.median(v), np.max(v)) for k, v in out.items()}
    r['mr_ratio'] = tuple(np.array(out['mr08']) / np.array(out['mr02']))
    r['mr_ratio'] = (min(r['mr_ratio']), float(np.median(r['mr_ratio'])), max(r['mr_ratio']))
    return r
if __name__ == '__main__':
    variants = {'note': {}}
    for arg in sys.argv[1:]:
        name, kv = arg.split(':', 1)
        variants[name] = {k: float(v) for k, v in (p.split('=') for p in kv.split(','))}
    for name, ch in variants.items():
        Q = dict(P); Q.update(ch)
        r = gates(Q)
        print(name, ' '.join(f'{k}={v[0]:.2f}/{v[1]:.2f}/{v[2]:.2f}' for k, v in r.items()))
