#!/usr/bin/env python3
"""Per-frame sun off-axis angle, NDC x,y and flare group (emission_source_gain_frame refused_state) in windows,
collapsed into runs of equal presence. usage: sun_burst_timeline.py LOG lo-hi [lo-hi ...]"""
import re, sys, math
L = sys.argv[1]; wins = [tuple(map(int, a.split('-'))) for a in sys.argv[2:]]
inw = lambda f: any(a <= f <= b for a, b in wins)
kv = re.compile(r'(\w+)=(\S+)'); cam = {}; sun = {}; grp = {}; cap = set()
fr_re = re.compile(rb' frame=(\d+)')
with open(L, 'rb') as f:
    for raw in f:
        if not raw.startswith((b'camera_state device=', b'shadow_replay_sun device=', b'emission_source_gain_frame', b'frame_end device=1')): continue
        m = fr_re.search(raw)
        if not m or not inw(int(m.group(1))): continue
        d = dict(kv.findall(raw.decode('latin1'))); fr = int(d['frame'])
        if raw.startswith(b'camera_state'):
            if d.get('valid') == '1': cam[fr] = ([[float(d[f'r{i}{j}']) for j in range(3)] for i in range(3)], float(d['p00']), float(d['p11']))
        elif raw.startswith(b'shadow_replay_sun'):
            if 'sun' in d: sun[fr] = [float(x) for x in d['sun'].split(',')]
        elif raw.startswith(b'emission'):
            grp[fr] = int(d['refused_state'])
        elif d.get('capture', '0') != '0': cap.add(fr)
runs = []
for fr in sorted(set(cam) & set(sun)):
    r, p00, p11 = cam[fr]; s = sun[fr]; v = [sum(s[i]*r[i][j] for i in range(3)) for j in range(3)]
    a = math.degrees(math.atan2(math.hypot(v[0], v[1]), v[2]))
    x, y = (v[0]/v[2]*p00, v[1]/v[2]*p11) if v[2] > 0 else (9, 9)
    key = grp.get(fr, 0) > 0
    if runs and runs[-1][0] == key and runs[-1][2] == fr - 1: runs[-1][2] = fr; runs[-1][3].append((a, x, y, fr in cap))
    else: runs.append([key, fr, fr, [(a, x, y, fr in cap)]])
print('present first..last n angle_min..max x_first,y_first -> x_last,y_last captured_frames')
for k, a, b, s in runs:
    ang = [t[0] for t in s]
    print(int(k), f'{a}..{b}', len(s), f'{min(ang):.1f}..{max(ang):.1f}', f'{s[0][1]:.3f},{s[0][2]:.3f} -> {s[-1][1]:.3f},{s[-1][2]:.3f}', sum(t[3] for t in s))
