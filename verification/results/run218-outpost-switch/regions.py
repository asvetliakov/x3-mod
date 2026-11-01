#!/usr/bin/env python3
"""Run 218 outpost switch: HDR luminance of the freighter_base station (node 3d8c0290) in the four F8 frames.
Station mask = RT2 written (.r >= 0) inside the box and RT2 z < 220000 (station cluster; the attached rock sits at
z >= 235000 and is the reference). upper = top 45 % of the station's rows (sun-lit cap), lower = rest (shadowed body),
hi = top 2 % luminance pixels of the station (specular/brightest patch). Split by RT2 share lane (.g >= 0 / < 0)."""
import numpy as np
D = '/mnt/c/X3/x3-modern-captures/'; W, H = 5120, 1440
B = {3571: (2520, 2760, 540, 740), 4148: (2520, 2800, 505, 770), 15021: (2520, 2760, 540, 740), 16134: (2420, 2700, 505, 765)}
STATE = {3571: 'band', 4148: 'opaque', 15021: 'band', 16134: 'opaque'}
for f, (x0, x1, y0, y1) in B.items():
    d = np.array(np.memmap(D + 'depth_1_%d.rgba32f' % f, dtype=np.float32).reshape(H, W, 4)[y0:y1, x0:x1])
    h = np.array(np.memmap(D + 'hdr_1_%d.rgba16f' % f, dtype=np.float16).reshape(H, W, 4)[y0:y1, x0:x1]).astype(np.float32)
    L = 0.2126 * h[..., 0] + 0.7152 * h[..., 1] + 0.0722 * h[..., 2]
    obj = (d[..., 0] >= 0) & np.isfinite(d[..., 2]) & (d[..., 2] > 0)
    st = obj & (d[..., 2] < 220000); rock = obj & (d[..., 2] >= 235000)
    ys = np.nonzero(st)[0]; cut = ys.min() + int(0.45 * (ys.max() - ys.min()))
    rows = np.arange(st.shape[0])[:, None]
    hi = st & (L >= np.percentile(L[st], 98))
    regs = (('station', st), ('upper', st & (rows < cut)), ('lower', st & (rows >= cut)), ('hi2%', hi), ('rock', rock))
    for name, m in regs:
        for cn, mm in (('all', m), ('share>=0', m & (d[..., 1] >= 0)), ('share<0', m & (d[..., 1] < 0))):
            n = int(mm.sum())
            if not n: continue
            print('%5d %-6s %-7s %-8s px=%6d L_mean=%.4f p10/50/90/99=%.4f/%.4f/%.4f/%.4f s_p50=%.2f z_p50=%.0f'
                  % (f, STATE[f], name, cn, n, L[mm].mean(), *np.percentile(L[mm], [10, 50, 90, 99]), np.median(d[..., 1][mm]), np.median(d[..., 2][mm])))
