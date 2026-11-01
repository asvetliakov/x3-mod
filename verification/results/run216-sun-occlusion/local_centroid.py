"""Run216: brightness-weighted centroid of a bgra8 5120x1440 capture inside a uv window, numpy slices only.
Weight = max(B,G,R) above a floor. Usage: python3 local_centroid.py <file> u0 u1 v0 v1 [floor=200]"""
import sys, numpy as np
W, H = 5120, 1440
p = sys.argv[1]; u0, u1, v0, v1 = map(float, sys.argv[2:6]); floor = int(sys.argv[6]) if len(sys.argv) > 6 else 200
a = np.memmap(p, dtype=np.uint8, mode='r', shape=(H, W, 4))
x0, x1, y0, y1 = int(u0 * W), int(u1 * W), int(v0 * H), int(v1 * H)
m = a[y0:y1, x0:x1, :3].max(axis=2).astype(np.float64)
w = np.where(m >= floor, m, 0.)
if w.sum() == 0: print(p.split('/')[-1], 'no pixels >= floor; max', m.max()); sys.exit()
ys, xs = np.mgrid[y0:y1, x0:x1]
cu = ((xs + .5) * w).sum() / w.sum() / W; cv = ((ys + .5) * w).sum() / w.sum() / H
n = int((w > 0).sum()); sat = int((a[y0:y1, x0:x1, :3].min(axis=2) >= 250).sum())
ys2, xs2 = np.nonzero(w)
print('%s window u=%.3f..%.3f v=%.3f..%.3f n>=%d: %d sat: %d centroid u=%.5f v=%.5f extent u=%.4f..%.4f v=%.4f..%.4f max=%d' % (
    p.split('/')[-1], u0, u1, v0, v1, floor, n, sat, cu, cv, (x0 + xs2.min()) / W, (x0 + xs2.max() + 1) / W,
    (y0 + ys2.min()) / H, (y0 + ys2.max() + 1) / H, int(m.max())))
