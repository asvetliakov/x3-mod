#!/usr/bin/env python3
"""Run 217: fraction of covered station pixels (HDR L >= 0.005, depth ignored) below 0.02 / 0.04, per box half."""
import numpy as np
D = '/mnt/c/X3/x3-modern-captures/'; W, H = 5120, 1440
REG = {5845: (2745, 2860, 505, 670), 7063: (2600, 2735, 625, 800)}
for f, (x0, x1, y0, y1) in REG.items():
    hz = np.array(np.memmap(D + 'hdr_1_%d.rgba16f' % f, dtype=np.float16).reshape(H, W, 4)[y0:y1, x0:x1]).astype(np.float32)
    L = 0.2126 * hz[..., 0] + 0.7152 * hz[..., 1] + 0.0722 * hz[..., 2]
    ys = int(0.45 * (y1 - y0))
    for name, Lr in (('station', L), ('upper', L[:ys]), ('lower', L[ys:])):
        c = Lr[Lr >= .005]
        print('%d %-7s cov=%5d L<0.02:%.3f L<0.04:%.3f p25=%.4f' % (f, name, c.size, (c < .02).mean(), (c < .04).mean(), np.percentile(c, 25)))
