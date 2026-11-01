#!/usr/bin/env python3
"""Run 217 outpost (freighter_base node 4bb6a2f8, target root 4bb6dfb8) triage: HDR luminance and RT2 share-lane split
per region. Boxes are full-res pixels picked from the present images (station body, attached rock excluded);
'upper' = top 45% of the box (lit disc), 'lower' = rest (shadowed body)."""
import numpy as np
D = '/mnt/c/X3/x3-modern-captures/'; W, H = 5120, 1440
REG = {5845: (2745, 2860, 505, 670), 7063: (2600, 2735, 625, 800)}
ROCK = {5845: (2685, 2740, 545, 630), 7063: (2540, 2595, 670, 760)}  # asteroid attached to the station, reference
for f, (x0, x1, y0, y1) in REG.items():
    dep = np.memmap(D + 'depth_1_%d.rgba32f' % f, dtype=np.float32).reshape(H, W, 4)
    hdr = np.memmap(D + 'hdr_1_%d.rgba16f' % f, dtype=np.float16).reshape(H, W, 4)
    ys = y0 + int(0.45 * (y1 - y0))
    for name, (a, b, c, d) in (('station', (x0, x1, y0, y1)), ('upper', (x0, x1, y0, ys)), ('lower', (x0, x1, ys, y1)), ('rock', ROCK[f])):
        dz = np.array(dep[c:d, a:b]); hz = np.array(hdr[c:d, a:b]).astype(np.float32)
        z, s = dz[..., 2], dz[..., 1]
        L = 0.2126 * hz[..., 0] + 0.7152 * hz[..., 1] + 0.0722 * hz[..., 2]
        obj = (dz[..., 0] >= 0) & np.isfinite(z) & (z > 0)
        for cname, m in (('all', obj), ('share>=0', obj & (s >= 0)), ('share<0', obj & (s < 0))):
            n = int(m.sum())
            if not n: print(f, name, cname, 'px=0'); continue
            print('%d %-7s %-8s px=%6d L_p10/50/90=%.4f/%.4f/%.4f L<0.005:%.3f z_p50=%.0f s_p50=%.3f'
                  % (f, name, cname, n, *np.percentile(L[m], [10, 50, 90]), (L[m] < .005).mean(), np.median(z[m]), np.median(s[m])))
