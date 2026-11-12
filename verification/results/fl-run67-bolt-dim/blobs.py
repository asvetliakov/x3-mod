#!/usr/bin/env python3
# Locate small bright objects in a crop of the pre-resolve hdr lane: pixels whose max channel exceeds the 7x7 median
# by > thr; connected components; per blob: centre, size, peak rgb, RT2 (z/w, g, w) at peak and at the 7x7 background
# median, RT1 motion at peak. args: dir frame x0 y0 x1 y1 [thr]
import numpy as np, sys
from numpy.lib.stride_tricks import sliding_window_view as swv
def median9(a):
    p = np.pad(a, 4, mode='edge'); return np.median(swv(p, (9, 9)), axis=(-1, -2))
def label(m):
    lab = np.zeros(m.shape, np.int32); n = 0; H, W = m.shape
    for y, x in zip(*np.nonzero(m)):
        if lab[y, x]: continue
        n += 1; st = [(y, x)]; lab[y, x] = n
        while st:
            a, b = st.pop()
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    u, v = a + dy, b + dx
                    if 0 <= u < H and 0 <= v < W and m[u, v] and not lab[u, v]: lab[u, v] = n; st.append((u, v))
    return lab, n
def objects(lab, n):
    out = []
    for i in range(1, n + 1):
        ys, xs = np.nonzero(lab == i); out.append((slice(ys.min(), ys.max() + 1), slice(xs.min(), xs.max() + 1)))
    return out
D, fr, x0, y0, x1, y1 = sys.argv[1], *map(int, sys.argv[2:7]); thr = float(sys.argv[7]) if len(sys.argv) > 7 else 0.3
hdr = np.memmap(f'{D}/hdr_1_{fr}.rgba16f', np.float16, 'r', shape=(1440, 5120, 4))
dep = np.memmap(f'{D}/depth_1_{fr}.rgba32f', np.float32, 'r', shape=(1440, 5120, 4))
mot = np.memmap(f'{D}/motion_1_{fr}.rgba32f', np.float32, 'r', shape=(1440, 5120, 4))
c = np.asarray(hdr[y0:y1, x0:x1, :4], np.float32); L = c[..., :3].max(-1)
bg = median9(L)
m = (L - bg) > thr
lab, n = label(m)
print(f'frame {fr} box {x0},{y0}-{x1},{y1} blobs {n}')
for i, sl in enumerate(objects(lab, n), 1):
    sub = np.where(lab[sl] == i, L[sl], -1); py, px = np.unravel_index(sub.argmax(), sub.shape)
    Y, X = sl[0].start + py, sl[1].start + px; gy, gx = Y + y0, X + x0
    d = dep[gy, gx]; mv = mot[gy, gx]
    print(f'  ({gx},{gy}) px={int((lab[sl]==i).sum())} rgb={c[Y,X,0]:.2f},{c[Y,X,1]:.2f},{c[Y,X,2]:.2f} a={c[Y,X,3]:.2f} bg={bg[Y,X]:.2f} '
          f'rt2 zw={d[0]:.6f} g={d[1]:.3f} w={d[2]:.0f} rt1={mv[0]:.3f},{mv[1]:.3f},{mv[2]:.4f},{mv[3]:.3f}')
