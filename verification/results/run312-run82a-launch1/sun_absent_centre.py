#!/usr/bin/env python3
"""Frames where the sun is on screen with off-axis angle < 25 deg but the refused_state=13 (sun flare) group of
emission_source_gain_frame is absent; prints frame, angle, ndc radius, p11 and grouped runs. usage: sun_absent_centre.py LOG"""
import re, sys, math
L = sys.argv[1]; kv = re.compile(r'(\w+)=(\S+)'); cam = {}; sun = {}; grp = {}
with open(L, 'rb') as f:
    for raw in f:
        if raw.startswith(b'camera_state device='):
            d = dict(kv.findall(raw.decode('latin1')))
            if d.get('valid') == '1': cam[int(d['frame'])] = ([[float(d[f'r{i}{j}']) for j in range(3)] for i in range(3)], float(d['p00']), float(d['p11']))
        elif raw.startswith(b'shadow_replay_sun device='):
            d = dict(kv.findall(raw.decode('latin1')))
            if 'sun' in d: sun[int(d['frame'])] = [float(x) for x in d['sun'].split(',')]
        elif raw.startswith(b'emission_source_gain_frame'):
            d = dict(kv.findall(raw.decode('latin1'))); grp[int(d['frame'])] = int(d['refused_state'])
rows = []
for fr in sorted(set(cam) & set(sun)):
    r, p00, p11 = cam[fr]; s = sun[fr]; v = [sum(s[i]*r[i][j] for i in range(3)) for j in range(3)]
    if v[2] <= 0: continue
    x, y = v[0]/v[2]*p00, v[1]/v[2]*p11
    if abs(x) > 1 or abs(y) > 1: continue
    a = math.degrees(math.atan2(math.hypot(v[0], v[1]), v[2]))
    if a < 25 and grp.get(fr, 0) == 0: rows.append((fr, a, math.hypot(x, y), p11, fr in grp))
print('absent near-centre frames', len(rows))
for fr, a, rr, p11, logged in rows: print(fr, f'angle={a:.1f} r={rr:.3f} p11={p11:.4f} esg_row={int(logged)}')
