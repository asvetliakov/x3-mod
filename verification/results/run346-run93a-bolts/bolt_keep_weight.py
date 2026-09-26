#!/usr/bin/env python3
"""Effective TAA history keep weight at bolt pixels: with taa_N = k * hist + (1 - k) * hdr_N and hist approximated by
the previous resolved frame at the same pixel (taa_{N-1}; the station and the sky move < 1 px/frame in these bursts),
k = <taa_N - hdr_N, taa_{N-1} - hdr_N> / |taa_{N-1} - hdr_N|^2 per pixel (only where |taa_{N-1} - hdr_N| > 0.5,
i.e. the bolt makes the current frame differ from history).  Classes and masks as bolt_band2.py.
usage (session dir): bolt_keep_weight.py g|b T FRAME [FRAME...]   (FRAME-1 must be a capture frame too)"""
import sys, json
import numpy as np
from numpy.lib.stride_tricks import sliding_window_view as sw
W, H = 5120, 1440
ch = {'g': 1, 'b': 2}[sys.argv[1]]; T = float(sys.argv[2]); oth = [c for c in range(3) if c != ch]
ld = lambda n, f: np.fromfile(f'{n}_1_{f}.rgba16f', np.float16).reshape(H, W, 4).astype(np.float32)[..., :3]
for f in sys.argv[3:]:
    h = ld('hdr', f); t = ld('taa', f); tp = ld('taa', int(f) - 1)
    d = np.fromfile(f'depth_1_{f}.rgba32f', np.float32).reshape(H, W, 4)
    bolt = (h[..., ch] - np.maximum(h[..., oth[0]], h[..., oth[1]])) > T
    geo = (d[..., 0] >= 0) & (d[..., 0] < 1); far = geo & (d[..., 2] > 20000); none = ~geo
    nf = sw(np.pad(bolt & far, 3), (7, 7)).any((-1, -2)); nn = sw(np.pad(bolt & none, 3), (7, 7)).any((-1, -2))
    band = bolt & nf & nn
    a = t - h; b = tp - h; bb = (b * b).sum(-1); ok = np.isfinite(bb) & (bb > 0.25)
    k = np.where(ok, (a * b).sum(-1) / np.where(ok, bb, 1), np.nan)
    r = {}
    for name, m in (('band_far', band & far & ok), ('band_none', band & none & ok), ('all_far', bolt & far & ok), ('all_none', bolt & none & ok)):
        r[name] = {'n': int(m.sum()), 'k_med': round(float(np.nanmedian(k[m])), 3) if m.any() else None,
                   'k_p25_p75': [round(float(v), 3) for v in np.nanpercentile(k[m], [25, 75])] if m.any() else None}
    print(f, json.dumps(r))
