#!/usr/bin/env python3
"""Lattice pitch of a shadow-map window: autocorrelation of the covered mask (map < 0.99999),
first ring of peaks (excluding the origin). usage: pitch.py CASCADE FRAME row0 col0 size [threshold] [min_peak_texels]"""
import sys, numpy as np
CAP = '/mnt/c/X3FL/x3-modern-captures/'; SIZES = {0: 2048, 1: 4096, 2: 4096, 3: 4096, 4: 2048}
EXT = {0: 250, 1: 1500, 2: 7500, 3: 37500, 4: 150000}
c, f, r0, c0, n = int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
N = SIZES[c]
m = np.array(np.memmap(CAP + f'shadow_map{c}_1_{f}.r32f', dtype='<f4', mode='r', shape=(N, N))[r0:r0 + n, c0:c0 + n])
thr = float(sys.argv[6]) if len(sys.argv) > 6 else 0.99999
cov = (m < thr).astype(np.float64)  # thr 0.7: the remote station only (its depth is ~0.53-0.55 in c0..c2)
print(f'c{c} frame {f} window r{r0} c{c0} n{n} covered={cov.mean():.3f}')
x = cov - cov.mean()
F = np.fft.rfft2(x); ac = np.fft.irfft2(F * np.conj(F), s=x.shape); ac = np.fft.fftshift(ac) / ac.max()
h = n // 2
RMIN = float(sys.argv[7]) if len(sys.argv) > 7 else 4.
peaks = []
for dy in range(-200, 201):
    for dx in range(-200, 201):
        rr = np.hypot(dx, dy)
        if rr < RMIN: continue
        v = ac[h + dy, h + dx]
        if v == ac[h + dy - 2:h + dy + 3, h + dx - 2:h + dx + 3].max() and v > 0.1:
            peaks.append((rr, dx, dy, v))
peaks.sort()
texel = 2 * EXT[c] / N
for rr, dx, dy, v in peaks[:8]:
    ang = np.degrees(np.arctan2(dy, dx))
    print(f'  peak d={rr:.2f} texels ({rr*texel:.2f} u) dx={dx} dy={dy} angle={ang:.1f} ac={v:.3f}')
