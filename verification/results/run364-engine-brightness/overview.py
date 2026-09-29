#!/usr/bin/env python3
"""Run 364: overview of the FP16 scene (hdr_1_F.rgba16f, pre-tonemap, EV 0) - write a downsampled AgX-free
log-luminance PNG and list connected regions with luminance > 1 (bbox, px, max, p99, mean, view depth)."""
import sys, numpy as np
from PIL import Image
d = sys.argv[1]; f = int(sys.argv[2]); W, H = (int(sys.argv[3]), int(sys.argv[4])) if len(sys.argv) > 4 else (5120, 1440)
P22, P32 = 1.00000298, -6.00001812
LUMA = np.array([0.2126, 0.7152, 0.0722])
c = np.fromfile(f'{d}/hdr_1_{f}.rgba16f', np.float16).reshape(H, W, 4)[..., :3].astype(np.float32)
z = np.fromfile(f'{d}/depth_1_{f}.rgba32f', np.float32).reshape(H, W, 4)[..., 0]
with np.errstate(divide='ignore', invalid='ignore'): vz = np.where(z >= 0, P32 / (z - P22), np.nan)
Y = c @ LUMA
mx = c.max(-1)
print(f'frame {f}: finite {np.isfinite(c).all()} Y max {Y.max():.2f} p99.9 {np.percentile(Y, 99.9):.3f}; px max-channel >1: {(mx > 1).sum()}, >4: {(mx > 4).sum()}')
if len(sys.argv) > 5:
    s = 4; im = np.log2(np.clip(Y[::s, ::s], 1e-4, None)); im = np.clip((im + 8) / 12, 0, 1)
    Image.fromarray((im * 255).astype(np.uint8)).save(sys.argv[5])
B = 16  # label 16x16 blocks holding any pixel > 1, 8-connected (no scipy)
bm = (mx > 1).reshape(H // B, B, W // B, B).any((1, 3))
lab = np.zeros(bm.shape, int); n = 0
for (by, bx) in zip(*np.nonzero(bm)):
    if lab[by, bx]: continue
    n += 1; st = [(by, bx)]; lab[by, bx] = n
    while st:
        a, b = st.pop()
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                y, x = a + dy, b + dx
                if 0 <= y < bm.shape[0] and 0 <= x < bm.shape[1] and bm[y, x] and not lab[y, x]: lab[y, x] = n; st.append((y, x))
full = np.repeat(np.repeat(lab, B, 0), B, 1)
rows = []
for i in range(1, n + 1):
    ys, xs = np.nonzero(lab == i)
    sl = (slice(ys.min() * B, (ys.max() + 1) * B), slice(xs.min() * B, (xs.max() + 1) * B))
    m = (full[sl] == i) & (mx[sl] > 1)
    if m.sum() < 50: continue
    y = mx[sl][m]; zz = vz[sl][m]
    rows.append((int(m.sum()), sl[1].start, sl[0].start, sl[1].stop, sl[0].stop, y.max(), np.percentile(y, 99), y.mean(), (y > 4).sum(), np.nanmedian(zz), np.nanmin(zz)))
rows.sort(reverse=True)
print('px>1  x0 y0 x1 y1  max p99 mean  px>4  depth_med depth_min')
for r in rows[:15]: print('%6d %4d %4d %4d %4d  %.2f %.2f %.2f %6d  %.0f %.0f' % r)
