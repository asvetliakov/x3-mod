#!/usr/bin/env python3
"""Station (depth < 0.7) / other-caster / empty share of ONE world window (half_u units around the median c1-first and c2-first receiver) in every cascade of a FL 71 A F8 frame: do the cascades agree about the same footprint? usage: same_window.py FRAME HALF_U"""
import sys, numpy as np
sys.path.insert(0, '/home/asvetl/x3-mod/verification/results/fl71a-shadow-lattice')
from project import rows, vec, CAP, W, H
f = sys.argv[1]; half_u = float(sys.argv[2])
cam, cas = rows(f)
R = np.array([[float(cam[f'r{i}{j}']) for j in range(3)] for i in range(3)]); t = vec(cam['t'])
d = np.memmap(CAP + f'depth_1_{f}.rgba32f', dtype='<f4', mode='r', shape=(H, W, 4))
st = 8; ys, xs = np.mgrid[0:H:st, 0:W:st]; z = np.array(d[::st, ::st, 2], np.float64); ok = z > 0
pw = (np.stack([((xs + .5) / W * 2 - 1) * z / float(cam['p00']), (1 - (ys + .5) / H * 2) * z / float(cam['p11']), z], -1) - t) @ R.T
# pick receiver points: the median c1-first pixel and the median c2-first pixel
chosen = np.full(z.shape, -1)
for c in sorted(cas):
    k = cas[c]; r_, u, fw, ce = [vec(k[n]) for n in ('right', 'up', 'forward', 'center')]
    E = float(k['extent']); L = float(k['depth_light']); B = float(k['depth_behind'])
    q = pw - ce; sx = q @ r_ / E; sy = q @ u / E; sz = (q @ fw + L) / (L + B)
    first = ok & (np.maximum(abs(sx), abs(sy)) <= 1) & (sz >= 0) & (sz <= 1) & (chosen < 0); chosen[first] = c
for pick in (1, 2):
    idx = np.argwhere(chosen == pick)
    if not len(idx): continue
    iy, ix = idx[len(idx) // 2]; P = pw[iy, ix]
    print(f'frame {f} receiver from c{pick} region, world {np.round(P,1)}, view z {z[iy,ix]:.0f}')
    for c in sorted(cas):
        k = cas[c]; r_, u, ce = vec(k['right']), vec(k['up']), vec(k['center']); E = float(k['extent']); N = int(k['size'])
        texel = 2 * E / N; half = int(round(half_u / texel))
        q = P - ce; col = int((q @ r_ / E * .5 + .5) * N); row = int((-(q @ u) / E * .5 + .5) * N)
        if not (half <= row < N - half and half <= col < N - half): print(f'  c{c}: window outside the map'); continue
        m = np.array(np.memmap(CAP + f'shadow_map{c}_1_{f}.r32f', dtype='<f4', mode='r', shape=(N, N))[row-half:row+half, col-half:col+half])
        stn = m < 0.7; oth = (~stn) & (m < 0.99999)
        print(f'  c{c}: texel {texel:.3f} u, window {2*half} texels = {2*half*texel:.0f} u: station {stn.mean():.3f} other_caster {oth.mean():.3f} empty {(m>=0.99999).mean():.3f}')
