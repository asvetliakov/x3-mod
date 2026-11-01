#!/usr/bin/env python3
"""Camera-relative speed (resolve.hlsl gateOpen 'relative' term, recomputed on the CPU) for the pixels of given boxes.

Usage: camera_relative_speed.py CAPDIR DRAWS_JSON FRAME x0 y0 x1 y1 [x0 y0 x1 y1 ...]
For each geometry pixel (RT1 alpha 1): routed previous raster position (RT1.xy * size - 0.5) against the camera path at
the pixel's own view w (RT2 .b): view = ndc-ray * w, world = (view - t_cur) inv(R_cur), prev = world R_prev + t_prev,
projected with the logged p00/p11 (camera_state, taa_resolve_replay.py convention conv=1). The routed previous UV is
jitter-free and the current raster is jittered, so the camera path starts at the unjittered raster point (px - jitter);
the sign was fixed by the own-frame residual (the camera path of a world-static pixel must match its routed motion). Prints median screen speed and median relative speed in px/frame;
the far weight's camera gate is open below 0.03 px and closed above 0.25 px (X3M_TAA_FAR_STABILISER lo/hi)."""
import json, re, sys, numpy as np
W, H = 5120, 1440
cap, dj, f = sys.argv[1], sys.argv[2], int(sys.argv[3]); boxes = list(map(int, sys.argv[4:]))
D = json.load(open(dj)); fr = D['frame_rows']
def cam(n):
    c = fr[str(n)]['camera_state']
    R = np.array([[float(c['r%d%d' % (i, j)]) for j in range(3)] for i in range(3)])
    t = np.array([float(v) for v in c['t'].split(',')]); return R, t, float(c['p00']), float(c['p11'])
Rc, tc, p00, p11 = cam(f); Rp, tp, q00, q11 = cam(f - 1)
mo = fr[str(f)]['motion_output_frame']; jx, jy = float(mo['jitter_x']), float(mo['jitter_y'])  # current raster jitter, px
mot = np.memmap('%s/motion_1_%d.rgba32f' % (cap, f), dtype='<f4', mode='r', shape=(H, W, 4))
rt2 = np.memmap('%s/depth_1_%d.rgba32f' % (cap, f), dtype='<f4', mode='r', shape=(H, W, 4))
for i in range(0, len(boxes), 4):
    x0, y0, x1, y1 = boxes[i:i+4]
    m = np.asarray(mot[y0:y1, x0:x1], np.float64); d = np.asarray(rt2[y0:y1, x0:x1], np.float64)
    ys, xs = np.mgrid[y0:y1, x0:x1]
    sel = (m[..., 3] == 1) & (d[..., 0] >= 0) & (d[..., 2] > 0)
    if sel.sum() == 0: print(boxes[i:i+4], 'no geometry'); continue
    px, py, z = xs[sel], ys[sel], d[..., 2][sel]
    nx, ny = 2*(px - jx)/W - 1, 1 - 2*(py - jy)/H
    view = np.stack([nx/p00, ny/p11, np.ones_like(nx)], -1) * z[:, None] - tc
    world = view @ np.linalg.inv(Rc); pv = world @ Rp + tp
    cx = (pv[:, 0]/pv[:, 2]*q00 + 1)/2*W; cy = (1 - pv[:, 1]/pv[:, 2]*q11)/2*H
    rx, ry = m[..., 0][sel]*W - 0.5, m[..., 1][sel]*H - 0.5
    scr = np.hypot(rx - px + jx, ry - py + jy); rel = np.hypot(rx - cx, ry - cy)
    print(boxes[i:i+4], 'px', int(sel.sum()), 'w_p50 %.0f' % np.median(z), 'screen_p50 %.3f' % np.median(scr),
          'relative_p50 %.3f p90 %.3f' % (np.median(rel), np.percentile(rel, 90)), 'open(<0.03) %.3f closed(>0.25) %.3f' % ((rel < 0.03).mean(), (rel > 0.25).mean()))
