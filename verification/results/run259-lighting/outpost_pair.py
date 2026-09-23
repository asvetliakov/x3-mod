"""Same-node pixel comparison of the military outpost (node 18eecaf8) between two F8 frames (coarse 9286, fine 10058).
Pixels: union box of the node's object_bounds rows in that frame, device z within the node's [zmin, zmax], sun share s>0.
Per-pixel view-space normal from the RT2 view depth (.b) by screen-space cross product of neighbour positions;
sun direction in view space = -normalize(row 2 of the frame's sun_shadow_apply_params rows4) (row 2 is the sun-depth axis,
depth grows along the light's travel direction). Halves: n.l > 0.2 (sun-facing) and n.l < -0.2 (averted).
Prints HDR luminance (hdr_<dev>_<frame>.rgba16f), s, lum*s and lum*(1-s) medians/means per half, and the correlation of
lum*s with max(n.l, 0) (a present sun term correlates positively; ambient-only flat shading does not).
usage: outpost_pair.py CAPDIR LOG NODE FRAME..."""
import sys, re
import numpy as np
cap, log, node, frames = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
kv = re.compile(r'(\w+)=(\S+)'); P = {f: None for f in frames}; B = {f: [] for f in frames}
with open(log, errors='replace') as fh:
    for line in fh:
        if line.startswith('sun_shadow_apply_params ') or (line.startswith('object_bounds ') and f'node={node}' in line):
            m = re.search(r' frame=(\d+) ', line)
            if not m or m.group(1) not in P: continue
            d = dict(kv.findall(line))
            if line.startswith('sun_'): P[m.group(1)] = d
            else: B[m.group(1)].append(d)
for f in frames:
    p = P[f]; W, H = int(p['width']), int(p['height']); m00, m11 = float(p['m00']), float(p['m11'])
    rt = np.fromfile(f'{cap}/depth_1_{f}.rgba32f', dtype='<f4').reshape(H, W, 4)
    hdr = np.fromfile(f'{cap}/hdr_1_{f}.rgba16f', dtype='<f2').reshape(H, W, 4).astype(np.float64)
    x0 = int(min(float(b['sx0']) for b in B[f])); x1 = int(max(float(b['sx1']) for b in B[f])) + 1
    y0 = int(min(float(b['sy0']) for b in B[f])); y1 = int(max(float(b['sy1']) for b in B[f])) + 1
    zmin = min(float(b['zmin']) for b in B[f]); zmax = max(float(b['zmax']) for b in B[f])
    ys, xs = np.mgrid[0:H, 0:W]
    w = rt[..., 2].astype(np.float64)
    V = np.stack([((xs + 0.5) / W * 2 - 1) * w / m00, (1 - (ys + 0.5) / H * 2) * w / m11, w], -1)
    dx = V[:, 2:, :] - V[:, :-2, :]; dy = V[2:, :, :] - V[:-2, :, :]
    n = np.zeros_like(V); n[1:-1, 1:-1] = np.cross(dx[1:-1], dy[:, 1:-1])
    n /= np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-30)
    n[n[..., 2] > 0] *= -1                                   # face the camera (view +z is forward)
    r2 = np.array([float(v) for v in p['rows4'].split(',')]).reshape(3, 4)[2, :3]
    L = -r2 / np.linalg.norm(r2)
    box = np.zeros((H, W), bool); box[y0:y1, x0:x1] = True
    zr = rt[..., 0]
    ok = (rt[..., 0] >= zmin - 2e-6) & (rt[..., 0] <= zmax + 2e-6) & (rt[..., 1] > 0)
    # same-surface neighbours only (reject silhouette normals)
    same = np.zeros((H, W), bool)
    same[1:-1, 1:-1] = ok[1:-1, :-2] & ok[1:-1, 2:] & ok[:-2, 1:-1] & ok[2:, 1:-1]
    sel = box & ok & same
    lum = 0.2126 * hdr[..., 0] + 0.7152 * hdr[..., 1] + 0.0722 * hdr[..., 2]
    s = rt[..., 1]; nl = (n @ L)
    sun = lum * s; rest = lum * (1 - s)
    print(f"== frame {f} node {node} box=({x0},{y0})-({x1},{y1}) pixels={int(sel.sum())} draws_bounded={len(B[f])} L_view={np.round(L, 3).tolist()}")
    for name, h in (('all', sel), ('sun-facing n.l>0.2', sel & (nl > 0.2)), ('averted n.l<-0.2', sel & (nl < -0.2))):
        if not h.any(): print(' ', name, 'none'); continue
        print(f"  {name:20s} px={int(h.sum()):6d} lum mean/p50/p90={lum[h].mean():.3f}/{np.median(lum[h]):.3f}/{np.percentile(lum[h], 90):.3f} "
              f"s mean={s[h].mean():.3f} sun(lum*s) mean/p50={sun[h].mean():.3f}/{np.median(sun[h]):.3f} rest mean={rest[h].mean():.4f}")
    a = sel & np.isfinite(nl)
    c = np.corrcoef(np.maximum(nl[a], 0), sun[a])[0, 1]
    fac, avr = sel & (nl > 0.2), sel & (nl < -0.2)
    print(f"  corr(max(n.l,0), lum*s)={c:.3f}  facing/averted lum ratio={lum[fac].mean() / max(lum[avr].mean(), 1e-9):.2f}  share facing={fac.sum() / max(sel.sum(), 1):.2f}")
