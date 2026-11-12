#!/usr/bin/env python3
# FL run 67 bolt dimming: per capture frame, the small objects in a box of the pre-resolve hdr lane (hdr_1, the FP16
# scene read before the first write-back), split into
#   flagged = any RT2.g > 1 (the late reshaded bolt draw, bolts-through-taa.md B'),
#   white   = unflagged, scene alpha >= 0.5 and grey (max-min < 0.15 of max) at the peak (the projectile meshes),
#   other   = everything else above the 9x9 median by thr;
# and per object the background under it from RT2 (.r = z/w, .b = clip w; -1 = no routed depth = space/backdrop) with
# the far weight farw = saturate((zw - d0) / (d1 - d0)), d0/d1 from far_gate (f0 30, f1 34, p00 0.4999979, p22, p32
# of camera_state; width 5120). args: dir frame x0 y0 x1 y1 [thr]
import numpy as np, sys
from numpy.lib.stride_tricks import sliding_window_view as swv
D, fr, x0, y0, x1, y1 = sys.argv[1], *map(int, sys.argv[2:7]); thr = float(sys.argv[7]) if len(sys.argv) > 7 else 0.3
P00, P22, P32, WID = 0.4999979, 1.00000298, -6.00001812, 5120
sc = P00 * WID * .5
d0 = np.float32(P22 + P32 / (30 * sc)); d1 = np.float32(P22 + P32 / (34 * sc))
hdr = np.memmap(f'{D}/hdr_1_{fr}.rgba16f', np.float16, 'r', shape=(1440, 5120, 4))
dep = np.memmap(f'{D}/depth_1_{fr}.rgba32f', np.float32, 'r', shape=(1440, 5120, 4))
c = np.asarray(hdr[y0:y1, x0:x1], np.float32); L = c[..., :3].max(-1)
R = np.asarray(dep[y0:y1, x0:x1], np.float32)
bg = np.median(swv(np.pad(L, 4, mode='edge'), (9, 9)), axis=(-1, -2))
m = ((L - bg) > thr) | (R[..., 1] > 1)
H, W = m.shape; lab = np.zeros(m.shape, np.int32); n = 0
for y, x in zip(*np.nonzero(m)):
    if lab[y, x]: continue
    n += 1; st = [(y, x)]; lab[y, x] = n
    while st:
        a, b = st.pop()
        for u in (a - 1, a, a + 1):
            for v in (b - 1, b, b + 1):
                if 0 <= u < H and 0 <= v < W and m[u, v] and not lab[u, v]: lab[u, v] = n; st.append((u, v))
rows = []
idx = np.argsort(lab.ravel(), kind='stable'); cuts = np.searchsorted(lab.ravel()[idx], np.arange(1, n + 2))
for i in range(n):
    p = idx[cuts[i]:cuts[i + 1]]; ys, xs = np.unravel_index(p, lab.shape)
    k = L[ys, xs].argmax(); Y, X = ys[k], xs[k]; rgb = c[Y, X, :3]; a = c[Y, X, 3]
    flagged = bool((R[ys, xs, 1] > 1).any())
    grey = (rgb.max() - rgb.min()) < 0.15 * max(rgb.max(), 1e-6)
    kind = 'flagged' if flagged else ('white' if a >= 0.5 and grey else 'other')
    zw = R[Y, X, 0]; w = R[Y, X, 2]
    farw = float(np.clip((zw - d0) / (d1 - d0), 0, 1)) if zw >= 0 else 0.0
    rows.append((kind, X + x0, Y + y0, len(p), float(rgb.max()), float(a), float(bg[Y, X]), float(zw), float(w), farw))
print(f'frame {fr} box {x0},{y0}-{x1},{y1} d0={d0:.7f} d1={d1:.7f} (w {30*sc:.0f}..{34*sc:.0f} units) objects {n}')
for kind in ('flagged', 'white', 'other'):
    s = [r for r in rows if r[0] == kind]
    if not s: print(f'  {kind:8s} 0'); continue
    sp = [r for r in s if r[7] < 0]; nr = [r for r in s if r[7] >= 0 and r[9] == 0]
    rp = [r for r in s if 0 < r[9] < 1]; fa = [r for r in s if r[9] >= 1]
    def med(v): return f'{np.median(v):.2f}' if v else '-'
    print(f'  {kind:8s} n={len(s):4d} px={sum(r[3] for r in s):6d} peak_med={med([r[4] for r in s])} alpha_med={med([r[5] for r in s])} '
          f'| over space(no depth) {len(sp)} near(farw=0) {len(nr)} ramp {len(rp)} far(farw=1) {len(fa)} '
          f'| bg w med near={med([r[8] for r in nr])} far={med([r[8] for r in fa])}')
