#!/usr/bin/env python3
"""Per frame: Titan-range receiver pixels (view z in [ZMIN, ZMAX]) whose nearest map texel lies inside the
remote station's closed footprint (station texels = depth < 0.7, closed with a box of +-CLOSE_M metres),
split into station-covered and 'hole' (footprint but not station: the lattice that reaches the hull).
usage: hole_share.py ZMIN ZMAX CLOSE_M FRAME..."""
import sys, numpy as np
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from project import rows, vec, CAP, W, H

def box(a, r):
    s = np.pad(a.astype(np.int32), r + 1).cumsum(0).cumsum(1)
    n = a.shape; k = 2 * r + 1
    return s[k:k + n[0], k:k + n[1]] - s[0:n[0], k:k + n[1]] - s[k:k + n[0], 0:n[1]] + s[0:n[0], 0:n[1]]

zmin, zmax, close_m = float(sys.argv[1]), float(sys.argv[2]), float(sys.argv[3])
for f in sys.argv[4:]:
    cam, cas = rows(f)
    R = np.array([[float(cam[f'r{i}{j}']) for j in range(3)] for i in range(3)]); t = vec(cam['t'])
    d = np.memmap(CAP + f'depth_1_{f}.rgba32f', dtype='<f4', mode='r', shape=(H, W, 4))
    st = 4; ys, xs = np.mgrid[0:H:st, 0:W:st]; z = np.array(d[::st, ::st, 2], np.float64); ok = (z >= zmin) & (z <= zmax)
    pw = (np.stack([((xs + .5) / W * 2 - 1) * z / float(cam['p00']), (1 - (ys + .5) / H * 2) * z / float(cam['p11']), z], -1) - t) @ R.T
    chosen = np.full(z.shape, -1); out = []
    for c in sorted(cas):
        k = cas[c]
        if k.get('valid') != '1': continue
        r_, u, fw, ce = [vec(k[n]) for n in ('right', 'up', 'forward', 'center')]
        E = float(k['extent']); L = float(k['depth_light']); B = float(k['depth_behind']); N = int(k['size'])
        q = pw - ce; sx = q @ r_ / E; sy = q @ u / E; sz = (q @ fw + L) / (L + B)
        first = ok & (np.maximum(abs(sx), abs(sy)) <= 1) & (sz >= 0) & (sz <= 1) & (chosen < 0); chosen[first] = c
        if not first.any() or c > 2: continue
        col = ((sx * .5 + .5) * N).astype(int).clip(0, N - 1)[first]; row = ((-sy * .5 + .5) * N).astype(int).clip(0, N - 1)[first]
        r0, r1, c0, c1 = row.min(), row.max() + 1, col.min(), col.max() + 1
        rad = max(1, int(round(close_m / (2 * E / N))))
        m = np.array(np.memmap(CAP + f'shadow_map{c}_1_{f}.r32f', dtype='<f4', mode='r', shape=(N, N))[max(r0 - 2*rad, 0):r1 + 2*rad, max(c0 - 2*rad, 0):c1 + 2*rad])
        stn = m < 0.7; k2 = (2 * rad + 1) ** 2
        closed = box(box(stn, rad) > 0, rad) == k2
        rr = row - max(r0 - 2*rad, 0); cc = col - max(c0 - 2*rad, 0)
        fp = closed[rr, cc]; cov = stn[rr, cc]
        out.append(f'c{c}: recv={first.sum()} footprint={fp.sum()} station={(fp & cov).sum()} holes={(fp & ~cov).sum()} hole_share={((fp & ~cov).sum() / max(fp.sum(), 1)):.3f}')
    print(f, ' | '.join(out))
