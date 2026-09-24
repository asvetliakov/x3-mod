#!/usr/bin/env python3
"""For each hdr_1_<frame>.rgba16f capture (5120x1440 RGBA16F) of a run dir: sun NDC from camera_state + shadow_replay_sun,
then max/mean luminance of the HDR target in a 41x41 px box at the sun's pixel and the frame's global max (sampled rows).
usage: sun_capture_pixels.py RUNDIR"""
import re, sys, glob, os, math, struct
import numpy as np
D = sys.argv[1]; L = sorted(glob.glob(f'{D}/session-*.log'))[0]
W, H = 5120, 1440
frames = sorted(int(re.search(r'hdr_1_(\d+)', p).group(1)) for p in glob.glob(f'{D}/hdr_1_*.rgba16f'))
want = set(frames); kv = re.compile(r'(\w+)=(\S+)'); cam = {}; sun = {}
with open(L, 'rb') as f:
    for raw in f:
        if raw.startswith(b'camera_state device=') or raw.startswith(b'shadow_replay_sun device='):
            m = re.search(rb' frame=(\d+) ', raw)
            if not m or int(m.group(1)) not in want: continue
            d = dict(kv.findall(raw.decode('latin1')))
            if raw.startswith(b'camera_state'):
                if d.get('valid') == '1': cam[int(d['frame'])] = ([[float(d[f'r{i}{j}']) for j in range(3)] for i in range(3)], float(d['p00']), float(d['p11']))
            elif 'sun' in d: sun[int(d['frame'])] = [float(x) for x in d['sun'].split(',')]
for fr in frames:
    if fr not in cam or fr not in sun: print(fr, 'no camera/sun'); continue
    r, p00, p11 = cam[fr]; s = sun[fr]
    v = [sum(s[i]*r[i][j] for i in range(3)) for j in range(3)]
    a = np.fromfile(f'{D}/hdr_1_{fr}.rgba16f', dtype=np.float16).reshape(H, W, 4).astype(np.float32)
    lum = 0.2126*a[..., 0] + 0.7152*a[..., 1] + 0.0722*a[..., 2]
    gmax = float(np.nanmax(lum)); gy, gx = np.unravel_index(np.nanargmax(lum), lum.shape)
    if v[2] <= 0: print(fr, f'p11={p11:.4f} sun behind camera; frame max lum {gmax:.2f} at ({gx},{gy})'); continue
    x = v[0]/v[2]*p00; y = v[1]/v[2]*p11
    px = (x*0.5+0.5)*W; py = (0.5-y*0.5)*H
    box = ''
    if 0 <= px < W and 0 <= py < H:
        x0, x1 = max(0, int(px)-20), min(W, int(px)+21); y0, y1 = max(0, int(py)-20), min(H, int(py)+21)
        b = lum[y0:y1, x0:x1]; box = f'box max {float(np.nanmax(b)):.2f} mean {float(np.nanmean(b)):.3f}'
    print(fr, f'p11={p11:.4f} sun ndc=({x:.3f},{y:.3f}) px=({px:.0f},{py:.0f}) axis_deg={math.degrees(math.atan2(math.hypot(v[0],v[1]),v[2])):.1f} {box}; frame max lum {gmax:.2f} at ({gx},{gy})')
