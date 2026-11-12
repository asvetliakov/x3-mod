#!/usr/bin/env python3
"""Screen-space lattice pitch in an hdr crop vs the reconstructed nearest-texel occlusion of the
same crop, and the sun-plane metres per screen peak vector. usage: screen_pitch.py FRAME CASCADE x0 y0 x1 y1"""
import sys, numpy as np
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from project import rows, vec, CAP, W, H
f, c = sys.argv[1], int(sys.argv[2]); x0, y0, x1, y1 = map(int, sys.argv[3:7])
cam, cas = rows(f)
R = np.array([[float(cam[f'r{i}{j}']) for j in range(3)] for i in range(3)]); t = vec(cam['t'])
d = np.memmap(CAP + f'depth_1_{f}.rgba32f', dtype='<f4', mode='r', shape=(H, W, 4))
ys, xs = np.mgrid[y0:y1, x0:x1]; z = np.array(d[y0:y1, x0:x1, 2], np.float64)
pw = (np.stack([((xs + .5) / W * 2 - 1) * z / float(cam['p00']), (1 - (ys + .5) / H * 2) * z / float(cam['p11']), z], -1) - t) @ R.T
k = cas[c]; r, u, fw, ce = [vec(k[n]) for n in ('right', 'up', 'forward', 'center')]
E = float(k['extent']); L = float(k['depth_light']); B = float(k['depth_behind']); N = int(k['size'])
q = pw - ce; sxm = q @ r; sym = q @ u; sz = (q @ fw + L) / (L + B)
col = ((sxm / E * .5 + .5) * N).astype(int).clip(0, N - 1); row = ((-sym / E * .5 + .5) * N).astype(int).clip(0, N - 1)
m = np.array(np.memmap(CAP + f'shadow_map{c}_1_{f}.r32f', dtype='<f4', mode='r', shape=(N, N)))
occl = (m[row, col] < sz - 1e-6).astype(np.float64)
h = np.array(np.memmap(CAP + f'hdr_1_{f}.rgba16f', dtype='<f2', mode='r', shape=(H, W, 4))[y0:y1, x0:x1, :3]).astype(np.float64)
lum = np.nan_to_num(h.mean(-1)); lum = np.log1p(lum.clip(0, None))
def ac(a):
    a = a - a.mean(); F = np.fft.fft2(a); r_ = np.real(np.fft.ifft2(F * np.conj(F))); r_ = np.fft.fftshift(r_); return r_ / r_.max()
def peaks(A, rmin=6, rmax=120, n=6):
    hy, hx = A.shape[0] // 2, A.shape[1] // 2; out = []
    for dy in range(-rmax, rmax + 1):
        for dx in range(0, rmax + 1):
            rr = np.hypot(dx, dy)
            if rr < rmin or rr > rmax or (dx == 0 and dy < 0): continue
            v = A[hy + dy, hx + dx]
            if v == A[hy + dy - 3:hy + dy + 4, hx + dx - 3:hx + dx + 4].max() and v > 0.05: out.append((v, dx, dy))
    out.sort(reverse=True); return out[:n]
# sun-plane metres per screen pixel (mean Jacobian over the crop)
jx = np.nanmedian(np.stack([np.diff(sxm, axis=1)[:-1, :], np.diff(sym, axis=1)[:-1, :]], -1).reshape(-1, 2), 0)
jy = np.nanmedian(np.stack([np.diff(sxm, axis=0)[:, :-1], np.diff(sym, axis=0)[:, :-1]], -1).reshape(-1, 2), 0)
print(f'frame {f} cascade {c} crop {x0},{y0}-{x1},{y1} occluded={occl.mean():.3f} sunplane m/px: dx->{np.round(jx,3)} dy->{np.round(jy,3)}')
for name, A in (('hdr', ac(lum)), ('occl', ac(occl))):
    for v, dx, dy in peaks(A):
        mvec = dx * jx + dy * jy
        print(f'  {name} peak dx={dx} dy={dy} px |{np.hypot(dx,dy):.1f}| ac={v:.3f} -> sun-plane {np.hypot(*mvec):.1f} m')
