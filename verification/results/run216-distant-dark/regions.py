#!/usr/bin/env python3
"""Run 216 distant-dark triage: per-region stats of the F8 dumps (no capture bytes kept).
Regions are full-res pixel boxes picked from the present images. Prints view depth (RT2.b),
HDR luminance and present luma over pixels with valid depth."""
import sys, numpy as np
D = '/mnt/c/X3/x3-modern-captures/'
W, H = 5120, 1440
REGIONS = {
    12898: {'asteroid_big': (1330, 1690, 255, 410), 'station': (2420, 2495, 125, 240), 'rock_by_station': (2515, 2560, 215, 275)},
    15576: {'asteroid_big': (980, 1470, 140, 480), 'station': (2385, 2450, 0, 120), 'rock_by_station': (2510, 2545, 140, 210)},
}
def load(kind, f, dt, ch):
    return np.memmap(D + '%s_1_%d.%s' % (kind, f, {'rgba32f': 'rgba32f', 'rgba16f': 'rgba16f', 'bgra8': 'bgra8'}[dt]),
                     dtype={'rgba32f': np.float32, 'rgba16f': np.float16, 'bgra8': np.uint8}[dt]).reshape(H, W, ch)
for f, regs in REGIONS.items():
    dep = load('depth', f, 'rgba32f', 4); hdr = load('hdr', f, 'rgba16f', 4); pre = load('present', f, 'bgra8', 4)
    for name, (x0, x1, y0, y1) in regs.items():
        dz = np.array(dep[y0:y1, x0:x1]); hz = np.array(hdr[y0:y1, x0:x1]).astype(np.float32); pz = np.array(pre[y0:y1, x0:x1]).astype(np.float32)
        z = dz[..., 2]; m = (dz[..., 0] >= 0) & np.isfinite(z) & (z > 0)
        L = 0.2126 * hz[..., 0] + 0.7152 * hz[..., 1] + 0.0722 * hz[..., 2]
        P = 0.2126 * pz[..., 2] + 0.7152 * pz[..., 1] + 0.0722 * pz[..., 0]
        if not m.any():
            print(f, name, 'no depth'); continue
        zp = np.percentile(z[m], [5, 50, 95])
        lp = np.percentile(L[m], [25, 50, 75, 95]); pp = np.percentile(P[m], [25, 50, 75, 95])
        dark = (P[m] < 8).mean()
        print('%d %-16s px=%6d z_p5/50/95=%.0f/%.0f/%.0f hdrL_p25/50/75/95=%.4f/%.4f/%.4f/%.4f present_p25/50/75/95=%.0f/%.0f/%.0f/%.0f dark(<8)=%.3f'
              % (f, name, m.sum(), *zp, *lp, *pp, dark))
