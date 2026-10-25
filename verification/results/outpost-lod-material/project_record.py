#!/usr/bin/env python3
"""Read-only: project stations/sadiablo/freighter_base record 0 (source catalogues) with the logged
object_matrix world/view/projection of one draw, rasterise per-face material ids with a z-buffer, and report
(a) the fit of the projected bounds to the logged object_bounds, (b) per material the visible pixel count with
the source cull modes (g_CullMode 1 = two-sided) and with every face one-sided as the merged material 23
draws it (g_CullMode 2 of dominant material 8), (c) pixels that change owner and what they show instead.
Usage (repo root): python3 verification/results/outpost-lod-material/project_record.py FRAME_LOG DRAW[,DRAW...] GAME [OUT.npz]
(matrices from the first draw; the fitted bounds are the union of the listed draws' object_bounds)"""
import sys, re, struct
import numpy as np
sys.path.insert(0, 'tools/analysis')
from pathlib import Path
import bob1, body_materials

log, game = sys.argv[1], Path(sys.argv[3])
draws = [int(x) for x in sys.argv[2].split(',')]; draw = draws[0]   # matrices from the first draw, bounds = union over the list
out = sys.argv[4] if len(sys.argv) > 4 else None
mats_rows = {}
cur = None; bounds = None
for l in open(log):
    m = re.match(r'draw device=\S+ frame=\S+ index=(\d+) ', l)
    if m:
        cur = int(m.group(1)); continue
    if cur not in draws: continue
    if l.startswith('object_bounds'):
        kv = dict(re.findall(r'(\w+)=(\S+)', l))
        b = [float(kv[k]) for k in ('sx0', 'sx1', 'sy0', 'sy1')]
        bounds = b if bounds is None else [min(bounds[0], b[0]), max(bounds[1], b[1]), min(bounds[2], b[2]), max(bounds[3], b[3])]
    if cur != draw: continue
    if l.startswith('object_matrix'):
        kv = dict(re.findall(r'(\w+)=(\S+)', l))
        vals = [struct.unpack('>f', bytes.fromhex(b))[0] for b in kv['bits'].split(',')]
        mats_rows.setdefault(kv['role'], {})[int(kv['row'])] = vals
M = {r: np.array([v[i] for i in range(4)], dtype=np.float64) for r, v in mats_rows.items() if len(v) == 4}
W, V, P = M['world'], M['view'], M['projection']
assets, _ = body_materials.original_assets(game)
tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, 'stations/sadiablo/freighter_base')), None)
rec = bob1.lods(tree)[0]
mats = bob1.materials(tree)
cull = {i: dict((n.decode(), v[0]) for n, t, v in m.get('params', []) if t != 8).get('g_CullMode') for i, m in enumerate(mats)}
pts = np.array([p[1:4] for p in rec['points']], dtype=np.float64)
faces, fmat = [], []
for part in rec['parts']:
    for g in part['groups']:
        for f in g['faces']:
            faces.append(f[:3]); fmat.append(g['material'])
faces = np.array(faces); fmat = np.array(fmat)

def project(k, flipz=1.0):
    v = pts * k
    v[:, 2] *= flipz
    h = np.c_[v, np.ones(len(v))] @ W @ V @ P
    ndc = h[:, :3] / h[:, 3:4]
    sx = (ndc[:, 0] + 1) * 0.5 * 5120
    sy = (1 - ndc[:, 1]) * 0.5 * 1440
    return sx, sy, h[:, 3]

best = None
for flipz in (1.0, -1.0):
    for k in np.geomspace(1e-6, 1e2, 400):
        sx, sy, w = project(k, flipz)
        if (w <= 0).any(): continue
        b = [sx.min(), sx.max(), sy.min(), sy.max()]
        err = sum(abs(a - c) for a, c in zip(b, bounds))
        if best is None or err < best[0]:
            best = (err, k, flipz, b)
# refine
err, k0, flipz, b = best
for k in np.linspace(k0 * 0.97, k0 * 1.03, 300):
    sx, sy, w = project(k, flipz)
    bb = [sx.min(), sx.max(), sy.min(), sy.max()]
    e = sum(abs(a - c) for a, c in zip(bb, bounds))
    if e < best[0]: best = (e, k, flipz, bb)
err, k, flipz, b = best
print(f'fit: k={k:.6g} flipz={flipz:+.0f} projected bounds x {b[0]:.1f}-{b[1]:.1f} y {b[2]:.1f}-{b[3]:.1f} vs logged x {bounds[0]}-{bounds[1]} y {bounds[2]}-{bounds[3]} (sum abs err {err:.1f} px)')
sx, sy, w = project(k, flipz)
x0, x1, y0, y1 = int(b[0]) - 2, int(b[1]) + 3, int(b[2]) - 2, int(b[3]) + 3
Wd, Hd = x1 - x0, y1 - y0
tri = faces
X = sx[tri] - x0; Y = sy[tri] - y0; Z = w[tri]
area = (X[:, 1] - X[:, 0]) * (Y[:, 2] - Y[:, 0]) - (X[:, 2] - X[:, 0]) * (Y[:, 1] - Y[:, 0])

def raster(keep):
    zb = np.full((Hd, Wd), np.inf); idb = np.full((Hd, Wd), -1, dtype=np.int32)
    for i in np.nonzero(keep)[0]:
        xs, ys, zs = X[i], Y[i], Z[i]
        a = area[i]
        if a == 0: continue
        bx0, bx1 = max(int(np.floor(xs.min())), 0), min(int(np.ceil(xs.max())), Wd - 1)
        by0, by1 = max(int(np.floor(ys.min())), 0), min(int(np.ceil(ys.max())), Hd - 1)
        if bx1 < bx0 or by1 < by0: continue
        gx, gy = np.meshgrid(np.arange(bx0, bx1 + 1) + 0.5, np.arange(by0, by1 + 1) + 0.5)
        w0 = ((xs[1] - gx) * (ys[2] - gy) - (xs[2] - gx) * (ys[1] - gy)) / a
        w1 = ((xs[2] - gx) * (ys[0] - gy) - (xs[0] - gx) * (ys[2] - gy)) / a
        w2 = 1 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not inside.any(): continue
        z = w0 * zs[0] + w1 * zs[1] + w2 * zs[2]
        sub = zb[by0:by1 + 1, bx0:bx1 + 1]; sid = idb[by0:by1 + 1, bx0:bx1 + 1]
        upd = inside & (z < sub)
        sub[upd] = z[upd]; sid[upd] = i
    return zb, idb

two = np.array([cull.get(m) == 1 for m in fmat])
allf = np.ones(len(faces), bool)
zb_all, id_all = raster(allf)
# winding convention: the sign held by most nearest faces of one-sided materials is the front side
vis = id_all[id_all >= 0]
one_vis = vis[~two[vis]]
front_sign = np.sign(np.median(np.sign(area[one_vis])))
front = np.sign(area) == front_sign
print(f'faces {len(faces)}; two-sided (g_CullMode 1) faces {two.sum()} in materials {sorted(set(fmat[two].tolist()))}; '
      f'front sign {front_sign:+.0f}: nearest one-sided faces front-facing {np.mean(np.sign(area[one_vis]) == front_sign):.3f}')
zb_src, id_src = raster(front | two)      # source: one-sided culled, two-sided kept
alpha_set = [i for i, m in enumerate(mats) if dict((n.decode(), v[0]) for n, t, v in m.get('params', []) if t != 8).get('g_ALPHATESTENABLE') or dict((n.decode(), v[0]) for n, t, v in m.get('params', []) if t != 8).get('g_AlphaBlendEnable')]
print('alpha materials (overlay alpha group, drawn two-sided with mat 19):', alpha_set)
zb_mrg, id_mrg = raster(front | np.isin(fmat, alpha_set))   # merged: opaque faces one-sided (material 23), alpha group two-sided
cov_src = id_src >= 0; cov_mrg = id_mrg >= 0
print(f'covered pixels: source {cov_src.sum()}, merged {cov_mrg.sum()} (lost silhouette {np.sum(cov_src & ~cov_mrg)})')
changed = cov_src & (id_src != id_mrg)
print(f'pixels whose nearest face changes: {changed.sum()} of {cov_src.sum()} ({changed.sum() / max(cov_src.sum(),1):.1%})')
bf = cov_src & ~front[np.maximum(id_src, 0)]
bf = bf & ~np.isin(fmat[np.maximum(id_src, 0)], alpha_set)
print(f'source pixels showing an opaque two-sided BACK face (culled in merged): {bf.sum()} ({bf.sum() / max(cov_src.sum(),1):.1%})')
from collections import Counter
c_src = Counter(fmat[id_src[cov_src]].tolist()); c_mrg = Counter(fmat[id_mrg[cov_mrg]].tolist())
c_bf = Counter(fmat[id_src[bf]].tolist())
c_instead = Counter(('bg' if id_mrg[y, x] < 0 else int(fmat[id_mrg[y, x]])) for y, x in zip(*np.nonzero(bf)))
print('mat cull  src_px  merged_px  backface_px_in_src')
for m in sorted(set(c_src) | set(c_mrg)):
    print(f'{m:3d} {cull.get(m)} {c_src.get(m,0):7d} {c_mrg.get(m,0):9d} {c_bf.get(m,0):9d}')
print('what the culled back-face pixels show in merged:', sorted(c_instead.items(), key=lambda t: -t[1]))
rows = np.nonzero(bf.any(1))[0]
if len(rows):
    hist = bf.sum(1)
    print('back-face pixel rows (screen y: count, top 12):', [(int(y + y0), int(hist[y])) for y in np.argsort(-hist)[:12]])
if out:
    np.savez_compressed(out, x0=x0, y0=y0, id_src=id_src, id_mrg=id_mrg, fmat=fmat, front=front, two=two, zb_src=zb_src)
