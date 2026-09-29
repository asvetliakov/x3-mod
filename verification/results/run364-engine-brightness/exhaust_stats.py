#!/usr/bin/env python3
"""Run 364 F8 burst: exhaust vs hull in the FP16 scene (hdr_1_F = engine/gamma-space values, pre-decode, pre-AgX).
Box = the target ship (Ocelot) crop. Exhaust = pixels with max channel > 1 (engine space) grown by 6 px and
whose max channel > 0.35; hull = box pixels with view depth < 20000 outside the grown exhaust mask.
Reports engine-space max/p99/mean of max-channel, linear (decode x^2.2, 1.0 = AgX reference white at EV 0),
AgX display luma (EV 0, as run364), pixel counts > 1 and > 4 (linear), bbox of the exhaust mask.
Usage: exhaust_stats.py <dir> first last x0 y0 x1 y1"""
import sys, numpy as np
sys.path.insert(0, '/Users/asvetl/x3-mod/verification/results/run340-run91a-pan-replay')
from replay_common import agx, LUMA
d = sys.argv[1]; a, b = int(sys.argv[2]), int(sys.argv[3]); x0, y0, x1, y1 = map(int, sys.argv[4:8]); W, H = 5120, 1440
P22, P32 = 1.00000298, -6.00001812
def grow(m, n):
    for _ in range(n):
        e = m.copy(); e[1:] |= m[:-1]; e[:-1] |= m[1:]; e[:, 1:] |= m[:, :-1]; e[:, :-1] |= m[:, 1:]; m = e
    return m
for f in range(a, b + 1):
    c = np.fromfile(f'{d}/hdr_1_{f}.rgba16f', np.float16).reshape(H, W, 4)[y0:y1, x0:x1, :3].astype(np.float64)
    z = np.fromfile(f'{d}/depth_1_{f}.rgba32f', np.float32).reshape(H, W, 4)[y0:y1, x0:x1, 0]
    with np.errstate(divide='ignore', invalid='ignore'): vz = np.where(z >= 0, P32 / (z - P22), np.inf)
    mx = c.max(-1); lin = np.maximum(c, 0) ** 2.2; lmx = lin.max(-1)
    core = mx > 1; ex = grow(core, 6) & (mx > 0.35); hull = (vz < 20000) & ~grow(core, 12)
    disp = agx(c, 0.0) @ LUMA
    def st(m, v): return (v[m].max(), np.percentile(v[m], 99), v[m].mean()) if m.any() else (0, 0, 0)
    ys, xs = np.nonzero(ex)
    print(f'frame {f}: exhaust px {int(ex.sum())} (engine>1 {int(core.sum())}; linear>1 {int((ex & (lmx > 1)).sum())}, linear>4 {int((ex & (lmx > 4)).sum())}) '
          f'bbox x {x0 + xs.min()}-{x0 + xs.max()} y {y0 + ys.min()}-{y0 + ys.max()} | hull px {int(hull.sum())}')
    for name, m in (('exhaust', ex), ('hull', hull)):
        e, l, dd = st(m, mx), st(m, lmx), st(m, disp)
        print(f'  {name:7s} engine max/p99/mean {e[0]:.3f} {e[1]:.3f} {e[2]:.3f} | linear {l[0]:.3f} {l[1]:.3f} {l[2]:.4f} | AgX luma {dd[0]:.3f} {dd[1]:.3f} {dd[2]:.3f}')
    if f == a:
        cc = c[core]; print('  core rgb mean', np.round(cc.mean(0), 3), 'min', np.round(cc.min(0), 3), 'max', np.round(cc.max(0), 3),
                             '| hist of max-channel in core:', np.histogram(mx[core], bins=[1, 1.5, 2, 2.5, 2.9, 2.99, 3.01])[0].tolist())
