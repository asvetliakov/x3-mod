#!/usr/bin/env python3
"""Bolt pixels (green excess e = G - max(R,B) > 0.25 in the pre-resolve FP16 scene hdr_1_N) split by what the
depth lane depth_1_N (.r = z/w of routed opaque draws, -1 = none) holds behind them: geometry (0 <= r < 1) or none.
'band' = bolt pixels whose 7x7 neighbourhood holds bolt pixels of both kinds (a bolt crossing a silhouette).
Also prints up to 4 adjacent geometry/sky pixel pairs inside the band with hdr rgb, depth .r and clip w (.b).
usage (from the session dir): bolt_band.py FRAME [FRAME...]"""
import sys, json
import numpy as np
from numpy.lib.stride_tricks import sliding_window_view as sw
W, H = 5120, 1440
res = {}
for f in sys.argv[1:]:
    h = np.fromfile(f'hdr_1_{f}.rgba16f', np.float16).reshape(H, W, 4).astype(np.float32)
    d = np.fromfile(f'depth_1_{f}.rgba32f', np.float32).reshape(H, W, 4)
    e = h[..., 1] - np.maximum(h[..., 0], h[..., 2])
    bolt = e > 0.25
    geo = (d[..., 0] >= 0) & (d[..., 0] < 1)
    ng = sw(np.pad(bolt & geo, 3), (7, 7)).any((-1, -2)); ns = sw(np.pad(bolt & ~geo, 3), (7, 7)).any((-1, -2))
    band = bolt & ng & ns
    med = lambda m: float(np.median(e[m])) if m.any() else None
    r = {'bolt_px': int(bolt.sum()), 'over_geo': int((bolt & geo).sum()), 'over_none': int((bolt & ~geo).sum()),
         'band_geo_n': int((band & geo).sum()), 'band_geo_e_med': med(band & geo),
         'band_none_n': int((band & ~geo).sum()), 'band_none_e_med': med(band & ~geo),
         'all_geo_e_med': med(bolt & geo), 'all_none_e_med': med(bolt & ~geo), 'pairs': []}
    ys, xs = np.nonzero(band & geo)
    for y, x in zip(ys, xs):
        for dx in (1, -1):
            if 0 <= x + dx < W and band[y, x + dx] and not geo[y, x + dx]:
                r['pairs'].append({'geo': [int(x), int(y), [round(float(v), 3) for v in h[y, x, :3]], float(d[y, x, 0]), float(d[y, x, 2])],
                                   'none': [int(x + dx), int(y), [round(float(v), 3) for v in h[y, x + dx, :3]], float(d[y, x + dx, 0])]})
                break
        if len(r['pairs']) >= 4: break
    res[f] = r
    print(f, json.dumps(r))
