#!/usr/bin/env python3
"""Run 75 B (run279) task 2: pixels brighter in a bullet-pair frame (6137, 6139, 6141: draws 9+70 present)
than in both neighbour frames (no pair), pre-resolve FP16 scene hdr_1_<f>.rgba16f; control on the no-pair
frames 6138, 6140, 6142. Components (8-connected) with centre, bbox, area, peak luminance, excess over the
neighbour max, colour, and depth-lane .r (z/w, -1 uncovered) at the component and the minimum covered z/w in a
+-12 px box around it. Usage: bolt_diff.py [threshold]"""
import sys, numpy as np
D = '/tmp/x3-bottleX3-run279'; W, H = 1920, 1080
hdr = lambda f: np.fromfile(f'{D}/hdr_1_{f}.rgba16f', np.float16).reshape(H, W, 4).astype(np.float32)
dep = lambda f: np.fromfile(f'{D}/depth_1_{f}.rgba32f', np.float32).reshape(H, W, 4)
lum = lambda a: 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
thr = float(sys.argv[1]) if len(sys.argv) > 1 else 0.3
A = {f: hdr(f) for f in range(6136, 6144)}; L = {f: lum(a) for f, a in A.items()}
def comps(mask):
    pts = set(zip(*np.nonzero(mask))); out = []
    while pts:
        st = [pts.pop()]; c = []
        while st:
            p = st.pop(); c.append(p)
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    q = (p[0] + dy, p[1] + dx)
                    if q in pts: pts.remove(q); st.append(q)
        out.append((np.array([p[0] for p in c]), np.array([p[1] for p in c])))
    return out
for f in range(6137, 6143):
    d = L[f] - np.maximum(L[f - 1], L[f + 1]); z = dep(f)[..., 0]
    cs = comps(d > thr)
    rows = []
    for ys, xs in cs:
        cy, cx = int(ys.mean()), int(xs.mean())
        box = z[max(cy - 12, 0):cy + 13, max(cx - 12, 0):cx + 13]; cov = box[box >= 0]
        rgb = A[f][ys, xs, :3].max(0)
        rows.append((float(d[ys, xs].max()), cx, cy, int(np.ptp(xs)) + 1, int(np.ptp(ys)) + 1, len(ys), float(L[f][ys, xs].max()),
                     rgb, float(z[ys, xs].max()), float(cov.min()) if cov.size else -1.0))
    rows.sort(key=lambda r: -r[0])
    print(f'frame {f} pair={"yes" if f % 2 else "no"} excess>{thr}: components {len(rows)} pixels {int((d > thr).sum())}')
    for r in rows[:10]:
        print('  (%d,%d) %dx%d area %d peakL %.2f excess %.2f rgb %.2f,%.2f,%.2f z_at %.5f z_min_box %.5f'
              % (r[1], r[2], r[3], r[4], r[5], r[6], r[0], *r[7], r[8], r[9]))
