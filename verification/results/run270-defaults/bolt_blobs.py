"""Run 72 A bolt blobs: bright small components in hdr readbacks. Usage: bolt_blobs.py CAPDIR FRAME [REF_FRAME] [EV]
With REF_FRAME, keeps only pixels whose luminance is >4x the reference frame's (bolt absent there). Prints components
(area px, bbox w/h, peak and mean scene luminance, exposed peak = peak*2^EV) and the hull reference (depth<1 median lum)."""
import sys, numpy as np
cap, f = sys.argv[1], int(sys.argv[2]); ref = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3] != '-' else None
ev = float(sys.argv[4]) if len(sys.argv) > 4 else 0.0
W, H = 1920, 1080
def hdr(n): return np.fromfile(f'{cap}/hdr_1_{n}.rgba16f', np.float16).reshape(H, W, 4).astype(np.float32)
def lum(a): return 0.2126*a[..., 0] + 0.7152*a[..., 1] + 0.0722*a[..., 2]
a = hdr(f); y = lum(a)
d = np.fromfile(f'{cap}/depth_1_{f}.rgba32f', np.float32).reshape(H, W, 4)[..., 0]
geo = (d < 0.99999) & (d > 0)
print(f'frame {f} ev {ev} max_lum {y.max():.3f} geo_px {geo.sum()} hull_median_lum {np.median(y[geo]) if geo.any() else 0:.4f} hull_p99_lum {np.percentile(y[geo],99) if geo.any() else 0:.4f}')
mx = np.max(a[..., :3], -1); mn = np.min(a[..., :3], -1); sat = (mx - mn) / np.maximum(mx, 1e-6)
mask = (y > 0.5) & (sat < 0.35)
if ref is not None:
    mask &= y > 4 * lum(hdr(ref)) + 0.05
pts = set(zip(*np.nonzero(mask))); rows = []; n = 0
while pts:
    st = [pts.pop()]; comp = []
    while st:
        p = st.pop(); comp.append(p)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                q = (p[0]+dy, p[1]+dx)
                if q in pts: pts.remove(q); st.append(q)
    n += 1; ys = np.array([c[0] for c in comp]); xs = np.array([c[1] for c in comp]); v = y[ys, xs]
    rows.append((len(comp), int(xs.max()-xs.min()+1), int(ys.max()-ys.min()+1), float(v.max()), float(v.mean()), int(xs.min()), int(ys.min()), float(np.median(sat[ys, xs]))))
rows.sort(key=lambda r: -r[3])
print(f'components {n} (lum>0.5, sat<0.35{", >4x ref "+str(ref) if ref is not None else ""}); area px distribution p50 {np.median([r[0] for r in rows]) if rows else 0}')
for r in rows[:12]:
    print(f'  area {r[0]} w {r[1]} h {r[2]} peak {r[3]:.2f} mean {r[4]:.2f} exposed_peak {r[3]*2**ev:.2f} x {r[5]} y {r[6]} sat {r[7]:.2f}')
