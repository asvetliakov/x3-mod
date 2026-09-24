#!/usr/bin/env python3
"""Run309 (or any run log): sun position in NDC per frame from camera_state (r[i][j] world axis i -> view axis j,
p00/p11) and shadow_replay_sun sun= (toward-sun, world). Prints on-screen ranges with min distance from centre."""
import re, sys, math
L = sys.argv[1]
kv = re.compile(r'(\w+)=(\S+)')
cam = {}; sun = {}
with open(L, 'rb') as f:
    for raw in f:
        if raw.startswith(b'camera_state device='):
            d = dict(kv.findall(raw.decode('latin1')))
            if d.get('valid') != '1': continue
            cam[int(d['frame'])] = ([[float(d[f'r{i}{j}']) for j in range(3)] for i in range(3)], float(d['p00']), float(d['p11']))
        elif raw.startswith(b'shadow_replay_sun device='):
            d = dict(kv.findall(raw.decode('latin1')))
            if 'sun' in d: sun[int(d['frame'])] = [float(x) for x in d['sun'].split(',')]
frames = sorted(set(cam) & set(sun)); print('frames', len(frames))
rows = []
for f in frames:
    r, p00, p11 = cam[f]; s = sun[f]
    v = [sum(s[i]*r[i][j] for i in range(3)) for j in range(3)]
    if v[2] <= 0: rows.append((f, None, p11)); continue
    x = v[0]/v[2]*p00; y = v[1]/v[2]*p11
    rows.append((f, (x, y, math.degrees(math.acos(max(-1, min(1, v[2]/math.sqrt(sum(c*c for c in v))))))), p11))
on = [(f, xy, p11) for f, xy, p11 in rows if xy and abs(xy[0]) <= 1 and abs(xy[1]) <= 1]
print('frames with sun on screen (|ndc|<=1)', len(on))
cur = None; rng = []
for f, xy, p11 in on:
    rad = math.hypot(xy[0], xy[1])
    if cur and f - cur[1] <= 2: cur[1] = f; cur[2] = min(cur[2], rad); cur[3] = min(cur[3], xy[2]); cur[4].add(round(p11, 4))
    else:
        if cur: rng.append(cur)
        cur = [f, f, rad, xy[2], {round(p11, 4)}]
if cur: rng.append(cur)
print('on-screen ranges: first last min_ndc_radius min_axis_deg p11')
for c in rng: print(c[0], c[1], round(c[2], 3), round(c[3], 2), sorted(c[4]))
