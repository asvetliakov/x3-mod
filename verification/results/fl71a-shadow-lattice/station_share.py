#!/usr/bin/env python3
"""Per captured frame: Titan-range pixels (view z in [ZMIN, ZMAX]) by selected cascade, split into
remote-station occluded (nearest map depth < 0.7, i.e. >= ~90 km sunward), other occluded, lit.
usage: station_share.py ZMIN ZMAX FRAME..."""
import sys, numpy as np
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from project import rows, vec, CAP, W, H
zmin, zmax = float(sys.argv[1]), float(sys.argv[2])
for f in sys.argv[3:]:
    cam, cas = rows(f)
    R = np.array([[float(cam[f'r{i}{j}']) for j in range(3)] for i in range(3)]); t = vec(cam['t'])
    d = np.memmap(CAP + f'depth_1_{f}.rgba32f', dtype='<f4', mode='r', shape=(H, W, 4))
    st = 4; ys, xs = np.mgrid[0:H:st, 0:W:st]; z = np.array(d[::st, ::st, 2], np.float64)
    ok = (z >= zmin) & (z <= zmax)
    pw = (np.stack([((xs + .5) / W * 2 - 1) * z / float(cam['p00']), (1 - (ys + .5) / H * 2) * z / float(cam['p11']), z], -1) - t) @ R.T
    chosen = np.full(z.shape, -1); parts = []
    for c in sorted(cas):
        k = cas[c]
        if k.get('valid') != '1': continue
        r, u, fw, ce = [vec(k[n]) for n in ('right', 'up', 'forward', 'center')]
        E = float(k['extent']); L = float(k['depth_light']); B = float(k['depth_behind']); N = int(k['size'])
        q = pw - ce; sx = q @ r / E; sy = q @ u / E; sz = (q @ fw + L) / (L + B)
        first = ok & (np.maximum(abs(sx), abs(sy)) <= 1) & (sz >= 0) & (sz <= 1) & (chosen < 0)
        chosen[first] = c
        if not first.any(): continue
        col = ((sx * .5 + .5) * N).astype(int).clip(0, N - 1); row = ((-sy * .5 + .5) * N).astype(int).clip(0, N - 1)
        md = np.array(np.memmap(CAP + f'shadow_map{c}_1_{f}.r32f', dtype='<f4', mode='r', shape=(N, N)))[row[first], col[first]]
        rz = sz[first]; station = md < 0.7; occ = md < rz - 1e-6
        parts.append(f'c{c}:n={first.sum()} station={station.sum()} other_occl={(occ & ~station).sum()} lit={(~occ).sum()}')
    print(f, 'titan_px', int(ok.sum()), ' '.join(parts))
