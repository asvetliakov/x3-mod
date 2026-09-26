#!/usr/bin/env python3
"""run339 bolt_band.py adapted for run346 (Run 93 A launch 1, single-copy rule on).
Bolt pixels: hue excess e = C - max(other two) > T in the pre-resolve FP16 scene hdr_1_N, C = G (burst 1, green bolts)
or B (burst 4, blue-white bolts).  Depth lane depth_1_N (.r z/w of routed draws, -1 none; .b clip w) splits them into
over FAR geometry (w > 20000: stations, asteroids), over NEAR geometry (own ship), over none.
'band' = bolt pixels whose 7x7 neighbourhood holds both far-geometry and no-depth bolt pixels (a silhouette crossing).
Per class: median e, and median bolt addition add = sum(rgb) - sum(local background), background = per-channel
median of the non-bolt pixels of the same class in the 9x9 window.  Also the resolved image taa_1_N at the same pixels
(e and add against the same background taken from taa_1_N) and up to 4 adjacent far-geo/none pixel pairs.
usage (from the session dir): bolt_band2.py g|b T FRAME [FRAME...]"""
import sys, json
import numpy as np
from numpy.lib.stride_tricks import sliding_window_view as sw
W, H = 5120, 1440
ch = {'g': 1, 'b': 2}[sys.argv[1]]; T = float(sys.argv[2]); oth = [c for c in range(3) if c != ch]
def excess(a): return a[..., ch] - np.maximum(a[..., oth[0]], a[..., oth[1]])
for f in sys.argv[3:]:
    h = np.fromfile(f'hdr_1_{f}.rgba16f', np.float16).reshape(H, W, 4).astype(np.float32)[..., :3]
    t = np.fromfile(f'taa_1_{f}.rgba16f', np.float16).reshape(H, W, 4).astype(np.float32)[..., :3]
    d = np.fromfile(f'depth_1_{f}.rgba32f', np.float32).reshape(H, W, 4)
    e = excess(h); bolt = e > T
    geo = (d[..., 0] >= 0) & (d[..., 0] < 1); far = geo & (d[..., 2] > 20000); near = geo & ~far; none = ~geo
    nf = sw(np.pad(bolt & far, 3), (7, 7)).any((-1, -2)); nn = sw(np.pad(bolt & none, 3), (7, 7)).any((-1, -2))
    band = bolt & nf & nn
    def add(img, mask, cls):
        ys, xs = np.nonzero(mask); vals = []
        for y, x in zip(ys, xs):
            y0, y1, x0, x1 = max(0, y - 4), min(H, y + 5), max(0, x - 4), min(W, x + 5)
            m = (~bolt[y0:y1, x0:x1]) & cls[y0:y1, x0:x1]
            if m.sum() < 5: continue
            bg = np.median(img[y0:y1, x0:x1][m], 0)
            vals.append((float(img[y, x].sum() - bg.sum()), float(bg.sum())))
        if not vals: return None, None, 0
        v = np.array(vals); return round(float(np.median(v[:, 0])), 3), round(float(np.median(v[:, 1])), 3), len(v)
    med = lambda a, m: round(float(np.median(a[m])), 3) if m.any() else None
    et = excess(t)
    r = {'bolt_px': int(bolt.sum()), 'over_far': int((bolt & far).sum()), 'over_near': int((bolt & near).sum()), 'over_none': int((bolt & none).sum())}
    for name, m, cls in (('band_far', band & far, far), ('band_none', band & none, none), ('all_far', bolt & far, far), ('all_none', bolt & none, none)):
        a, bg, n = add(h, m, cls); at, bgt, _ = add(t, m, cls)
        r[name] = {'n': int(m.sum()), 'e_hdr': med(e, m), 'e_taa': med(et, m), 'add_hdr': a, 'bg_hdr': bg, 'add_taa': at, 'bg_taa': bgt}
    r['pairs'] = []
    ys, xs = np.nonzero(band & far)
    for y, x in zip(ys, xs):
        for dx in (1, -1):
            if 0 <= x + dx < W and band[y, x + dx] and none[y, x + dx]:
                r['pairs'].append({'far': [int(x), int(y), [round(float(v), 3) for v in h[y, x]], round(float(d[y, x, 0]), 8), round(float(d[y, x, 2]))],
                                   'none': [int(x + dx), int(y), [round(float(v), 3) for v in h[y, x + dx]]]})
                break
        if len(r['pairs']) >= 4: break
    print(f, json.dumps(r))
