"""Run216: locate the drawn sun in a 5120x1440 bgra8 capture (present or lens plane), numpy only.
Bright pixels = min(B,G,R) >= threshold (white disc). Reports their count, the 40x40-px block grid of bright
counts (top blocks), and the centroid of the bright pixels inside the densest 400x400-px window.
Usage: python3 find_sun_blob.py <file.bgra8> [threshold=250] [width=5120] [height=1440]"""
import sys, numpy as np
path = sys.argv[1]; thr = int(sys.argv[2]) if len(sys.argv) > 2 else 250
W = int(sys.argv[3]) if len(sys.argv) > 3 else 5120; H = int(sys.argv[4]) if len(sys.argv) > 4 else 1440
a = np.fromfile(path, dtype=np.uint8, count=W * H * 4).reshape(H, W, 4)
lum = a[..., :3].min(axis=2); mx = a[..., :3].max(axis=2)
bright = lum >= thr
print('%s max=%d mean=%.2f bright=%d' % (path.split('/')[-1], int(mx.max()), float(a[..., :3].mean()), int(bright.sum())))
B = 40
grid = bright.reshape(H // B, B, W // B, B).sum(axis=(1, 3))
idx = np.argsort(grid.ravel())[::-1][:6]
for k in idx:
    gy, gx = divmod(int(k), grid.shape[1])
    if grid[gy, gx]: print('  block u=%.4f v=%.4f bright=%d' % ((gx + .5) * B / W, (gy + .5) * B / H, grid[gy, gx]))
if bright.any():
    # densest 400x400 window (10x10 blocks)
    from numpy.lib.stride_tricks import sliding_window_view
    s = sliding_window_view(grid, (10, 10)).sum(axis=(2, 3))
    gy, gx = np.unravel_index(int(np.argmax(s)), s.shape)
    y0, x0 = gy * B, gx * B
    ys, xs = np.nonzero(bright[y0:y0 + 400, x0:x0 + 400])
    print('  densest window bright=%d centroid u=%.5f v=%.5f extent_u=%.4f..%.4f extent_v=%.4f..%.4f' % (
        len(xs), (x0 + xs.mean() + .5) / W, (y0 + ys.mean() + .5) / H, (x0 + xs.min()) / W, (x0 + xs.max() + 1) / W,
        (y0 + ys.min()) / H, (y0 + ys.max() + 1) / H))
