#!/usr/bin/env python3
"""fire_tune.py [--shipped | seed count]: the fire material's constants against fire-material.md section 6 and the body
erosion gates on fire_model.py's replica (hull burst R(t) 100 px, death body 90 px, medians over the seeds). --shipped
prints the shipped constants' failing gates and measures; otherwise a random search over SPACE (the 2026-11-07 run:
seed 1, 160 samples, best 14 of 18 at index 32, failing only the gaps and the erosion gates; the shipped constants are
its rounding with the note's gain 1.6)."""
import sys, json, random, numpy as np
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import fire_model as model
SEEDS = [3.1, 17.3, 29.9, 41.7]
def evaluate(k):
    res = {}
    for name, R, L, E, cg, cx, rs in (('hull', 100, 400, 1.5, 2.0, 0.5, 1.25), ('death', 90, 2000, 1.5, 5.0, 0.35, 1.4)):
        acc = {}
        for sd in SEEDS:
            for frac in (0.1, 0.2, 0.3, 0.5, 0.9):
                g = cg * (1 - frac / cx) ** 2 if frac < cx else 0
                T = 1 - frac if name == 'hull' else max(0.0, 1 - 0.8 * frac)
                ms = model.measures(model.render(int(R * 3.2), R, frac, T, E, g, frac * L, sd, 1.0, k, rs_over_r=rs), R)
                for kk, v in ms.items():
                    acc.setdefault((frac, kk), []).append(v)
        m = {kk: float(np.median(v)) for kk, v in acc.items()}
        res[name] = m
    return res
def score(res):
    ok = {}
    for name, m in res.items():
        g = lambda f, k: m.get((f, k), 0)
        ok[name + ':thin'] = 0.10 <= g(0.3, 'thin') <= 0.35
        ok[name + ':gaps'] = g(0.3, 'gaps') >= 0.35
        ok[name + ':licks'] = g(0.3, 'licks_disc') >= 2.0
        ok[name + ':aniso'] = g(0.3, 'aniso') <= 0.75
        ok[name + ':hot'] = g(0.2, 'hot') >= 0.25 and g(0.2, 'hot_r') <= 0.5
        ok[name + ':red'] = 0.05 <= g(0.2, 'cold') <= 0.25 and g(0.2, 'cold_r') >= 0.65
        ok[name + ':mid'] = g(0.2, 'mid') <= 0.5
        ok[name + ':core'] = g(0.1, 'core') >= 3.0 and g(0.1, 'out') <= 1.5
        ok[name + ':erode'] = g(0.1, 'lit_in') >= 0.8 and 0.35 <= g(0.5, 'lit_in') <= 0.75 and g(0.9, 'lit_in') <= 0.15
    return ok
SPACE = dict(soft=(0.08, 0.35), gain=(1.3, 2.6), thr0=(-0.1, 0.15), thr_t=(0.2, 0.6), thr_r=(0.2, 0.6), heat_soft=(0.2, 0.8),
             rim_t=(0.1, 0.4), rcool=(0.0, 0.8))
if __name__ == '__main__' and '--shipped' in sys.argv:
    SEEDS[:] = [3.1, 17.3, 29.9, 41.7, 53.2, 7.7, 11.9, 61.3]
    res = evaluate(dict(model.K))
    print('failing', [n_ for n_, v in score(res).items() if not v])
    for name, m in res.items():
        print(name, ' '.join('%s@%.1f=%.3f' % (kk, f, v) for (f, kk), v in sorted(m.items()) if kk != 'licks_fill'))
    sys.exit(0)
if __name__ == '__main__':
    random.seed(int(sys.argv[1]) if len(sys.argv) > 1 else 1)
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 100
    best = None
    for i in range(n):
        k = dict(model.K)
        if i:
            for p, (a, b) in SPACE.items():
                k[p] = round(random.uniform(a, b), 3)
        else:
            k['rcool'] = 0.0
        res = evaluate(k)
        ok = score(res)
        s = sum(ok.values())
        if best is None or s > best[0]:
            best = (s, {p: k[p] for p in SPACE if p in k}, [n_ for n_, v in ok.items() if not v])
            print(i, s, len(ok), json.dumps(best[1]), 'fail', best[2], flush=True)
