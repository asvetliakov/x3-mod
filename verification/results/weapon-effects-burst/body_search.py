import itertools, numpy as np, sys
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import body_sim as B
seeds = range(10)
def passes(Q):
    ok = 0; detail = []
    for sd in seeds:
        r = {}
        for x in (0.1, 0.5, 0.9):
            rgb, rr = B.image(x, sd, Q, N=140); l = rgb.max(-1); inR = rr <= 1
            r[x] = ((l > 0.1 * l.max()) & inR).sum() / inR.sum()
        mr = []
        for x in (0.2, 0.8):
            rgb, rr = B.image(x, sd, Q, N=140); l = rgb.max(-1); lit = l > 0.1 * l.max()
            mr.append(rr[lit].mean() if lit.any() else 0)
        rgb, rr = B.image(0.3, sd, Q, N=140); l = rgb.max(-1); inR = rr <= 1
        dark = ((l < 0.1 * l.max()) & inR).sum() / inR.sum()
        rgb, rr = B.image(0.2, sd, Q, N=140)
        st = B.RAMP[1:] / B.RAMP[1:].max(1, keepdims=True)
        T = []
        for sel in (rr <= 0.3, (rr >= 0.7) & (rr <= 1)):
            cm = rgb[sel].sum(0); cm = cm / max(cm.max(), 1e-9)
            T.append(0.25 * (1 + np.argmin(((st - cm) ** 2).sum(1))))
        g = [r[0.1] >= 0.8, 0.35 <= r[0.5] <= 0.75, r[0.9] <= 0.15, mr[0] > 0 and mr[1] <= 0.8 * mr[0], dark >= 0.15,
             T[0] >= 0.75, T[1] <= 0.5]
        detail.append(g)
        ok += all(g)
    return ok, np.mean(np.array(detail), 0)
grid = dict(e0=(0.15, 0.25), et=(0.3, 0.45), er=(0.2, 0.4, 0.6), adv=(0.5, 1.5), hsoft=(0.2, 0.35))
best = []
for vals in itertools.product(*grid.values()):
    Q = dict(B.P); Q.update(dict(zip(grid, vals)))
    ok, d = passes(Q)
    best.append((ok, vals, d))
    print(ok, dict(zip(grid, vals)), np.round(d, 2), flush=True)
best.sort(key=lambda t: -t[0])
print('BEST', best[:5])
