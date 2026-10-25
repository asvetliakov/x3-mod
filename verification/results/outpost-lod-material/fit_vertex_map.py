#!/usr/bin/env python3
"""Read-only: fit the body-point -> vertex map v = (p - c) * k (one k, one centre c, z sign) of
freighter_base record 0 jointly to the logged object_bounds unions of the distant (frame 2410 draws 22,23)
and close (frame 5735 draws 18..40) captures with their logged world/view/projection. Prints the fit.
Usage (repo root): python3 .../fit_vertex_map.py F2410_LOG F5735_LOG GAME"""
import sys, re, struct
import numpy as np
sys.path.insert(0, 'tools/analysis')
from pathlib import Path
import bob1, body_materials

def read(log, draws):
    rows, cur, bounds = {}, None, None
    for l in open(log):
        m = re.match(r'draw device=\S+ frame=\S+ index=(\d+) ', l)
        if m: cur = int(m.group(1)); continue
        if cur not in draws: continue
        if l.startswith('object_bounds'):
            kv = dict(re.findall(r'(\w+)=(\S+)', l)); b = [float(kv[k]) for k in ('sx0', 'sx1', 'sy0', 'sy1')]
            bounds = b if bounds is None else [min(bounds[0], b[0]), max(bounds[1], b[1]), min(bounds[2], b[2]), max(bounds[3], b[3])]
        if cur == draws[0] and l.startswith('object_matrix'):
            kv = dict(re.findall(r'(\w+)=(\S+)', l))
            rows.setdefault(kv['role'], {})[int(kv['row'])] = [struct.unpack('>f', bytes.fromhex(b))[0] for b in kv['bits'].split(',')]
    M = {r: np.array([v[i] for i in range(4)]) for r, v in rows.items() if len(v) == 4}
    return M['world'] @ M['view'] @ M['projection'], np.array(bounds)

f1, f2, game = sys.argv[1], sys.argv[2], Path(sys.argv[3])
views = [read(f1, [22, 23]), read(f2, list(range(18, 41)))]
assets, _ = body_materials.original_assets(game)
tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, 'stations/sadiablo/freighter_base')), None)
pts = np.array([p[1:4] for p in bob1.lods(tree)[0]['points']], dtype=np.float64)
used = sorted({i for part in bob1.lods(tree)[0]['parts'] for g in part['groups'] for f in g['faces'] for i in f[:3]})
pts = pts[used]

def bnds(MVP, x, sz):
    k, cx, cy, cz = x
    v = (pts - [cx, cy, cz]) * k; v[:, 2] *= sz
    h = np.c_[v, np.ones(len(v))] @ MVP
    if (h[:, 3] <= 0).any(): return None
    sx = (h[:, 0] / h[:, 3] + 1) * 2560; sy = (1 - h[:, 1] / h[:, 3]) * 720
    return np.array([sx.min(), sx.max(), sy.min(), sy.max()])

def cost(x, sz):
    e = 0
    for MVP, b in views:
        p = bnds(MVP, x, sz)
        if p is None: return 1e12
        e += np.abs(p - b).sum()
    return e

def nelder(f, x0, steps, it=4000):
    n = len(x0); S = [np.array(x0, float)] + [np.array(x0, float) + np.eye(n)[i] * steps[i] for i in range(n)]
    F = [f(s) for s in S]
    for _ in range(it):
        o = np.argsort(F); S = [S[i] for i in o]; F = [F[i] for i in o]
        c = np.mean(S[:-1], 0); r = c + (c - S[-1]); fr = f(r)
        if fr < F[0]:
            e = c + 2 * (c - S[-1]); fe = f(e)
            S[-1], F[-1] = (e, fe) if fe < fr else (r, fr)
        elif fr < F[-2]: S[-1], F[-1] = r, fr
        else:
            k = c + 0.5 * (S[-1] - c); fk = f(k)
            if fk < F[-1]: S[-1], F[-1] = k, fk
            else:
                S = [S[0]] + [S[0] + 0.5 * (s - S[0]) for s in S[1:]]; F = [F[0]] + [f(s) for s in S[1:]]
    i = int(np.argmin(F)); return S[i], F[i]

ctr = (pts.min(0) + pts.max(0)) / 2
for sz in (1.0, -1.0):
    best = None
    for k0 in (1.0 / 65536, 1.5e-5, 2e-5):
        for c0 in (np.zeros(3), ctr):
            x, e = nelder(lambda x: cost(x, sz), [k0, *c0], [k0 * 0.1, 2e5, 2e5, 2e5])
            if best is None or e < best[1]: best = (x, e)
    x, e = best
    print(f'zsign {sz:+.0f}: k={x[0]:.6g} (x65536 = {x[0] * 65536:.4f}) centre=({x[1]:.0f},{x[2]:.0f},{x[3]:.0f}) sum abs bound err {e:.1f} px')
    for (MVP, b), name in zip(views, ('2410 d22-23', '5735 d18-40')):
        print('   ', name, 'proj', np.round(bnds(MVP, x, sz), 1).tolist(), 'logged', b.tolist())
print('bbox centre of the used points', ctr.round(0).tolist(), 'half extent', ((pts.max(0) - pts.min(0)) / 2).round(0).tolist())
