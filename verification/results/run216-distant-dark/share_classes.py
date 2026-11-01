#!/usr/bin/env python3
"""Run 216 distant-dark triage: classify depth-covered pixels by the RT2 share lane
(.g: >= 0 an opaque sun-lane receiver drawn with the original-fill share variant;
< 0 not a receiver, e.g. a fade-band draw or an RT2 owner writing -1) and print HDR
luminance per class, per frame and for the big-asteroid region."""
import numpy as np
D = '/mnt/c/X3/x3-modern-captures/'; W, H = 5120, 1440
REG = {12898: (1330, 1690, 255, 410), 15576: (980, 1470, 140, 480)}
for f in (12898, 15576):
    dep = np.memmap(D + 'depth_1_%d.rgba32f' % f, dtype=np.float32).reshape(H, W, 4)
    hdr = np.memmap(D + 'hdr_1_%d.rgba16f' % f, dtype=np.float16).reshape(H, W, 4)
    for name, (x0, x1, y0, y1) in (('frame', (0, W, 0, H)), ('asteroid', REG[f])):
        dz = np.array(dep[y0:y1, x0:x1]); hz = np.array(hdr[y0:y1, x0:x1]).astype(np.float32)
        z, s = dz[..., 2], dz[..., 1]
        L = 0.2126 * hz[..., 0] + 0.7152 * hz[..., 1] + 0.0722 * hz[..., 2]
        obj = (dz[..., 0] >= 0) & np.isfinite(z) & (z > 0)
        for cname, m in (('share>=0', obj & (s >= 0)), ('share<0', obj & (s < 0))):
            n = int(m.sum())
            if not n:
                print(f, name, cname, 'px=0'); continue
            print('%d %-8s %-9s px=%7d L_p10/50/90=%.4f/%.4f/%.4f L==0:%.3f L<0.005:%.3f z_p50=%.0f s_p50=%.3f'
                  % (f, name, cname, n, *np.percentile(L[m], [10, 50, 90]), (L[m] == 0).mean(), (L[m] < .005).mean(),
                     np.median(z[m]), np.median(s[m])))
