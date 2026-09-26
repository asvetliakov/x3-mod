#!/usr/bin/env python3
"""TAA state at bolt pixels, split as in bolt_band2.py (far geometry w > 20000 / none): taa_age_1_N (.r32f) and
motion_1_N (.rgba32f, all four channels) medians and value histograms, plus the resolved/pre-resolve ratio of the
bolt addition per pixel.  usage (session dir): bolt_taa_state.py g|b T FRAME [FRAME...]"""
import sys, collections
import numpy as np
from numpy.lib.stride_tricks import sliding_window_view as sw
W, H = 5120, 1440
ch = {'g': 1, 'b': 2}[sys.argv[1]]; T = float(sys.argv[2]); oth = [c for c in range(3) if c != ch]
for f in sys.argv[3:]:
    h = np.fromfile(f'hdr_1_{f}.rgba16f', np.float16).reshape(H, W, 4).astype(np.float32)[..., :3]
    d = np.fromfile(f'depth_1_{f}.rgba32f', np.float32).reshape(H, W, 4)
    age = np.fromfile(f'taa_age_1_{f}.r32f', np.float32).reshape(H, W)
    mo = np.fromfile(f'motion_1_{f}.rgba32f', np.float32).reshape(H, W, 4)
    e = h[..., ch] - np.maximum(h[..., oth[0]], h[..., oth[1]]); bolt = e > T
    geo = (d[..., 0] >= 0) & (d[..., 0] < 1); far = geo & (d[..., 2] > 20000); none = ~geo
    nf = sw(np.pad(bolt & far, 3), (7, 7)).any((-1, -2)); nn = sw(np.pad(bolt & none, 3), (7, 7)).any((-1, -2))
    band = bolt & nf & nn
    for name, m in (('band_far', band & far), ('band_none', band & none), ('all_far', bolt & far), ('all_none', bolt & none), ('nonbolt_far', far & ~bolt), ('nonbolt_none', none & ~bolt)):
        if not m.any(): print(f, name, 'n=0'); continue
        a = age[m]; mv = mo[m]
        hist = collections.Counter(np.round(a, 2).tolist()).most_common(4)
        print(f, name, f'n={int(m.sum())}', 'age med', round(float(np.median(a)), 3), 'top', hist,
              'motion med', [round(float(np.median(mv[:, c])), 4) for c in range(4)])
