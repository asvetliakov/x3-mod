#!/usr/bin/env python3
"""Run 73 B, task 1: the corvette's bolts in the F8 burst (frames 15495-15502, 1920x1080 pre-resolve
FP16 hdr readbacks). Components of blue-dominant pixels (B > 1.35 R) with luminance > 0.4 and peak > 1.5 in
the window ahead of the ship; the fixed muzzle/hull lights at (863|873|1046|1056, 811|855) are listed apart.
Sizes are what the Run73 DLL drew (draw 90 already expanded by the 3,8 rule, draw 9 native).
Usage: bolt_components.py CAPDIR"""
import sys, numpy as np
cap = sys.argv[1]; W, H = 1920, 1080
x0, y0, x1, y1 = 700, 300, 1220, 900
fixed = lambda x, y: (abs(y - 812) <= 3 or abs(y - 856) <= 3) and min(abs(x - c) for c in (863, 873, 1046, 1056)) <= 3
for f in range(15495, 15503):
    a = np.fromfile(f'{cap}/hdr_1_{f}.rgba16f', np.float16).reshape(H, W, 4).astype(np.float32)[y0:y1, x0:x1]
    lum = 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
    m = (lum > 0.4) & (a[..., 2] > 1.35 * a[..., 0])
    pts = set(zip(*np.nonzero(m))); comps = []
    while pts:
        st = [pts.pop()]; c = []
        while st:
            p = st.pop(); c.append(p)
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    q = (p[0] + dy, p[1] + dx)
                    if q in pts: pts.remove(q); st.append(q)
        ys = np.array([p[0] for p in c]); xs = np.array([p[1] for p in c])
        peak = float(lum[ys, xs].max())
        if peak <= 1.5: continue
        comps.append((int(xs.mean()) + x0, int(ys.mean()) + y0, int(np.ptp(xs)) + 1, int(np.ptp(ys)) + 1, len(c), round(peak, 2)))
    flight = [c for c in comps if not fixed(c[0], c[1]) and c[4] >= 3]
    lights = [c for c in comps if fixed(c[0], c[1])]
    print(f'frame {f} bolts_in_flight {len(flight)} ' + ' '.join(f'({x},{y} {w}x{h}px area {n} peak {p})' for x, y, w, h, n, p in flight)
          + f' | fixed_lights {len(lights)}')
