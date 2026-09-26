#!/usr/bin/env python3
"""Run 91 A pan blur: median RT1 displacement (px/frame) of routed pixels beyond 25,000 view units per capture
frame, against the logged camera rotation (motion_output_frame camera_rotation_deg) times the centre focal
length p00 * W / 2 = 1280 px/rad (22.34 px/deg).
Usage: capture_pan_vs_rotation.py <capture dir> <session log> <first frame> <count>"""
import re, sys, numpy as np
W, H = 5120, 1440
P22, P32 = 1.00000298, -6.00001812
d, log, first, count = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
want = set(range(first - 2, first + count))
rot = {}
with open(log, errors='replace') as fh:
    for line in fh:
        if line.startswith('motion_output_frame '):
            m = re.search(r' frame=(\d+) ', line)
            if m and int(m.group(1)) in want:
                rot[int(m.group(1))] = float(re.search(r'camera_rotation_deg=(\S+)', line).group(1))
ys, xs = np.mgrid[0:H, 0:W]
for fr in range(first, first + count):
    m = np.memmap(f'{d}/motion_1_{fr}.rgba32f', np.float32, 'r', shape=(H, W, 4))
    z = np.memmap(f'{d}/depth_1_{fr}.rgba32f', np.float32, 'r', shape=(H, W, 4))[:, :, 0]
    sel = (z >= 0) & (m[:, :, 3] == 1)
    with np.errstate(divide='ignore', invalid='ignore'):
        vz = np.where(sel, P32 / (z - P22), 0)
    sel &= vz >= 25000
    dx = (m[:, :, 0] * W - (xs + 0.5))[sel]; dy = (m[:, :, 1] * H - (ys + 0.5))[sel]
    print('frame %d n=%d dx p50 %.2f dy p50 %.2f |d| p50 %.2f | rot(f) %.4f deg -> %.2f px, rot(f-1) %.4f -> %.2f px' % (
        fr, sel.sum(), np.median(dx), np.median(dy), np.median(np.hypot(dx, dy)),
        rot.get(fr, float('nan')), rot.get(fr, float('nan')) * 22.34, rot.get(fr - 1, float('nan')), rot.get(fr - 1, float('nan')) * 22.34))
