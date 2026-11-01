#!/usr/bin/env python3
"""Run 218 opaque frames: station pixels (same mask as regions.py) binned by RT2.g sun share; band frames all -1."""
import numpy as np
D = '/mnt/c/X3/x3-modern-captures/'; W, H = 5120, 1440
B = {4148: (2520, 2800, 505, 770), 16134: (2420, 2700, 505, 765)}
for f, (x0, x1, y0, y1) in B.items():
    d = np.array(np.memmap(D + 'depth_1_%d.rgba32f' % f, dtype=np.float32).reshape(H, W, 4)[y0:y1, x0:x1])
    h = np.array(np.memmap(D + 'hdr_1_%d.rgba16f' % f, dtype=np.float16).reshape(H, W, 4)[y0:y1, x0:x1]).astype(np.float32)
    L = 0.2126 * h[..., 0] + 0.7152 * h[..., 1] + 0.0722 * h[..., 2]
    st = (d[..., 0] >= 0) & (d[..., 2] > 0) & (d[..., 2] < 220000); s = d[..., 1]
    for lo, hi in ((0, 1e-6), (1e-6, .3), (.3, .6), (.6, .8), (.8, 1.01)):
        m = st & (s >= lo) & (s < hi)
        if m.sum(): print('%5d share[%.2f,%.2f) px=%6d frac=%.3f L_mean=%.4f p50=%.4f p90=%.4f' % (f, lo, hi, m.sum(), m.sum() / st.sum(), L[m].mean(), *np.percentile(L[m], [50, 90])))
