"""Run 72 B: do the third-person bolt components persist at the same pixels between consecutive capture frames?
Usage: bolt_motion.py CAPDIR F0 F1 X0 Y0 X1 Y1  (1920x1080 hdr readback, components of luminance > 0.5 in the window).
Per consecutive pair prints the component counts, how many components of frame f+1 have a frame-f component within
1 px / 3 px of their centroid (a TAA history hit needs the same pixel), and the median nearest-centroid distance."""
import sys, numpy as np
cap, f0, f1 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]); x0, y0, x1, y1 = map(int, sys.argv[4:8])
W, H = 1920, 1080
def comps(f):
    a = np.fromfile(f'{cap}/hdr_1_{f}.rgba16f', np.float16).reshape(H, W, 4).astype(np.float32)[y0:y1, x0:x1]
    y = 0.2126*a[..., 0] + 0.7152*a[..., 1] + 0.0722*a[..., 2]
    pts = set(zip(*np.nonzero(y > 0.5))); out = []
    while pts:
        st = [pts.pop()]; comp = []
        while st:
            p = st.pop(); comp.append(p)
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    q = (p[0]+dy, p[1]+dx)
                    if q in pts: pts.remove(q); st.append(q)
        out.append(np.mean(np.array(comp, np.float32), 0))
    return np.array(out)
prev = comps(f0)
for f in range(f0 + 1, f1 + 1):
    cur = comps(f)
    if len(prev) and len(cur):
        d = np.sqrt(((cur[:, None, :] - prev[None, :, :]) ** 2).sum(-1)).min(1)
        print(f'frames {f-1}->{f} components {len(prev)}->{len(cur)} within_1px {(d <= 1).sum()} within_3px {(d <= 3).sum()} nearest_px median {np.median(d):.1f} p10 {np.percentile(d,10):.1f}')
    prev = cur
