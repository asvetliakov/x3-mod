"""Run 72 A: pixels present in bolt frame F but absent in both neighbour frames A,B (bolt draw absent there). Usage: bolt_diff.py CAPDIR F A B EV"""
import sys, numpy as np
cap, f, a, b, ev = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), float(sys.argv[5])
W, H = 1920, 1080
def lum(n):
    x = np.fromfile(f'{cap}/hdr_1_{n}.rgba16f', np.float16).reshape(H, W, 4).astype(np.float32)
    return 0.2126*x[..., 0] + 0.7152*x[..., 1] + 0.0722*x[..., 2]
y = lum(f); r = np.maximum(lum(a), lum(b))
for k in (2, 4):
    m = (y > k * r + 0.1)
    ys, xs = np.nonzero(m)
    print(f'frame {f} ratio>{k}: px {m.sum()}', end=' ')
    if m.sum():
        v = y[m]; print(f'bbox x {xs.min()}-{xs.max()} y {ys.min()}-{ys.max()} peak {v.max():.2f} mean {v.mean():.2f} p90 {np.percentile(v,90):.2f} exposed_peak {v.max()*2**ev:.2f} ref_mean {r[m].mean():.3f}')
    else: print()
# histogram of row bands for ratio>4
m = (y > 4 * r + 0.1); ys, xs = np.nonzero(m)
if m.sum():
    H2, xe, ye = np.histogram2d(xs, ys, bins=[16, 9], range=[[0, W], [0, H]])
    idx = np.argsort(H2.ravel())[::-1][:6]
    print(' top 120x120 cells (x0,y0,px):', [(int(xe[i//9]), int(ye[i%9]), int(H2.ravel()[i])) for i in idx])
