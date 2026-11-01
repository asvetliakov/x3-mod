#!/usr/bin/env python3
"""Run 217: same boxes as regions.py, but over pixels WITHOUT a valid RT2 depth (refused zwrite-0 draws leave RT2 at
the sentinel), restricted to covered pixels (HDR L >= 0.005, sky/starfield below). Also present luma."""
import numpy as np
D = "/mnt/c/X3/x3-modern-captures/"; W, H = 5120, 1440
REG = {5845: (2745, 2860, 505, 670), 7063: (2600, 2735, 625, 800)}
for f, (x0, x1, y0, y1) in REG.items():
    dep = np.array(np.memmap(D + 'depth_1_%d.rgba32f' % f, dtype=np.float32).reshape(H, W, 4)[y0:y1, x0:x1])
    hz = np.array(np.memmap(D + 'hdr_1_%d.rgba16f' % f, dtype=np.float16).reshape(H, W, 4)[y0:y1, x0:x1]).astype(np.float32)
    pz = np.array(np.memmap(D + 'present_1_%d.bgra8' % f, dtype=np.uint8).reshape(H, W, 4)[y0:y1, x0:x1]).astype(np.float32)
    L = 0.2126 * hz[..., 0] + 0.7152 * hz[..., 1] + 0.0722 * hz[..., 2]
    P = 0.2126 * pz[..., 2] + 0.7152 * pz[..., 1] + 0.0722 * pz[..., 0]
    z = dep[..., 2]; obj = (dep[..., 0] >= 0) & np.isfinite(z) & (z > 0)
    ys = int(0.45 * (y1 - y0)); half = np.zeros_like(obj); half[:ys] = True
    for name, hm in (('station', np.ones_like(obj)), ('upper', half), ('lower', ~half)):
        for cname, m in (('depth', obj & hm), ('nodepth_cov', ~obj & hm & (L >= .005))):
            n = int(m.sum())
            if not n: print(f, name, cname, 'px=0'); continue
            print('%d %-7s %-11s px=%6d hdrL_p10/50/90=%.4f/%.4f/%.4f present_p50/90=%.0f/%.0f'
                  % (f, name, cname, n, *np.percentile(L[m], [10, 50, 90]), *np.percentile(P[m], [50, 90])))
