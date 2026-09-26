#!/usr/bin/env python3
"""Displayed contrast of the bolt: same bolt masks and classes as bolt_band2.py; for each class the median 8-bit
presented luma (present_1_N, BGRA8, after TAA + tonemap + sharpen) at the bolt pixels minus the local background
(median of non-bolt same-class pixels in the 9x9 window), and the same for the pre-resolve scene put through
x/(1+x) as a crude display proxy (codes 0..255) to show the hull is not near white.
usage (session dir): bolt_present.py g|b T FRAME [FRAME...]"""
import sys, json
import numpy as np
from numpy.lib.stride_tricks import sliding_window_view as sw
W, H = 5120, 1440
ch = {'g': 1, 'b': 2}[sys.argv[1]]; T = float(sys.argv[2]); oth = [c for c in range(3) if c != ch]
lw = np.array([0.2126, 0.7152, 0.0722], np.float32)
for f in sys.argv[3:]:
    h = np.fromfile(f'hdr_1_{f}.rgba16f', np.float16).reshape(H, W, 4).astype(np.float32)[..., :3]
    p = np.fromfile(f'present_1_{f}.bgra8', np.uint8).reshape(H, W, 4)[..., [2, 1, 0]].astype(np.float32)
    d = np.fromfile(f'depth_1_{f}.rgba32f', np.float32).reshape(H, W, 4)
    bolt = (h[..., ch] - np.maximum(h[..., oth[0]], h[..., oth[1]])) > T
    geo = (d[..., 0] >= 0) & (d[..., 0] < 1); far = geo & (d[..., 2] > 20000); none = ~geo
    nf = sw(np.pad(bolt & far, 3), (7, 7)).any((-1, -2)); nn = sw(np.pad(bolt & none, 3), (7, 7)).any((-1, -2))
    band = bolt & nf & nn
    hl = h @ lw; pl = p @ lw; tl = 255 * (hl / (1 + hl))
    r = {}
    for name, m, cls in (('band_far', band & far, far), ('band_none', band & none, none)):
        vals = []
        for y, x in zip(*np.nonzero(m)):
            y0, y1, x0, x1 = max(0, y - 4), min(H, y + 5), max(0, x - 4), min(W, x + 5)
            k = (~bolt[y0:y1, x0:x1]) & cls[y0:y1, x0:x1]
            if k.sum() < 5: continue
            vals.append((hl[y, x], np.median(hl[y0:y1, x0:x1][k]), pl[y, x] - np.median(pl[y0:y1, x0:x1][k]),
                         np.median(pl[y0:y1, x0:x1][k]), tl[y, x] - np.median(tl[y0:y1, x0:x1][k])))
        if vals:
            v = np.median(np.array(vals), 0)
            r[name] = {'n': len(vals), 'hdr_luma_bolt': round(float(v[0]), 3), 'hdr_luma_bg': round(float(v[1]), 3),
                       'present_delta_codes': round(float(v[2]), 1), 'present_bg_codes': round(float(v[3]), 1), 'hdr_proxy_delta_codes': round(float(v[4]), 1)}
    print(f, json.dumps(r))
