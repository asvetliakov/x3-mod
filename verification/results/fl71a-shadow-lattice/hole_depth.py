#!/usr/bin/env python3
"""At the 220 u window of same_window.py (frame 9607): c2 depth at the texels c1 leaves as holes versus the c1 plate depth (same surface filled at the coarser texel, or a layer behind)."""
import sys, numpy as np
sys.path.insert(0, '/home/asvetl/x3-mod/verification/results/fl71a-shadow-lattice')
from project import rows, vec, CAP
f = '9607'; P = np.array([-509.7, 100.3, 2980.6]); half_u = 110.
cam, cas = rows(f)
def window(c):
    k = cas[c]; r_, u, fw, ce = [vec(k[n]) for n in ('right','up','forward','center')]; E = float(k['extent']); N = int(k['size'])
    L = float(k['depth_light']); B = float(k['depth_behind']); texel = 2*E/N; half = int(round(half_u/texel))
    q = P - ce; col = int((q @ r_ / E * .5 + .5) * N); row = int((-(q @ u) / E * .5 + .5) * N)
    m = np.array(np.memmap(CAP + f'shadow_map{c}_1_{f}.r32f', dtype='<f4', mode='r', shape=(N, N))[row-half:row+half, col-half:col+half], np.float64)
    # world depth toward the light relative to the cascade centre: z_world = sz*(L+B) - L
    return m * (L + B) - L, texel
w1, t1 = window(1); w2, t2 = window(2)
# resample c2 to c1's grid (nearest)
idx = (np.arange(w1.shape[0]) * t1 / t2).astype(int).clip(0, w2.shape[0]-1)
w2r = w2[np.ix_(idx, idx)]
station1 = w1 < -60000; station2 = w2r < -60000
hole = ~station1 & station2
print('c1 station share %.3f, c2 (resampled) %.3f; c1-hole & c2-covered share %.3f' % (station1.mean(), station2.mean(), hole.mean()))
for name, sel in (('c1 plate texels', station1), ('c1 hole texels, c2 depth there', hole)):
    src = w1 if name.startswith('c1 plate') else w2r
    v = src[sel]
    print(name, 'n', v.size, 'depth toward light: p5 %.0f p50 %.0f p95 %.0f' % tuple(np.percentile(v, [5, 50, 95])))
v = w2r[station1]; print('c2 depth at c1-plate texels: p5 %.0f p50 %.0f p95 %.0f' % tuple(np.percentile(v, [5, 50, 95])))
d = w2r[hole] - np.median(w1[station1]); print('c2 fill depth minus c1 plate median: p5 %.0f p50 %.0f p95 %.0f' % tuple(np.percentile(d, [5, 50, 95])))
