#!/usr/bin/env python3
"""Presence of the refused_state=13 draw group of emission_source_gain_frame (the group present on every sun-on-screen frame
at F=0x3470) against the sun NDC radius, per F period, in 0.025 bins up to 0.4; plus the sun ndc of absent frames.
usage: sun_group_radius.py LOG"""
import re, sys, math, collections
L = sys.argv[1]; kv = re.compile(r'(\w+)=(\S+)')
cam = {}; sun = {}; grp = {}
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
res = collections.defaultdict(lambda: [0, 0]); absent_xy = collections.defaultdict(list)
for fr in sorted(set(cam) & set(sun)):
    r, p00, p11 = cam[fr]; s = sun[fr]; v = [sum(s[i]*r[i][j] for i in range(3)) for j in range(3)]
    if v[2] <= 0: continue
    x, y = v[0]/v[2]*p00, v[1]/v[2]*p11
    if abs(x) > 1 or abs(y) > 1: continue
    per = 'F=0x3470' if abs(p11-1.7778) < 1e-3 else ('F=0x471c' if abs(p11-1.1188) < 1e-3 else ('F=0x4000' if abs(p11-1.3333) < 1e-3 else 'other'))
    rb = min(int(math.hypot(x, y)/0.025), 16)
    has = grp.get(fr, 0) > 0
    res[(per, rb)][0 if has else 1] += 1
    if not has: absent_xy[per].append((round(x, 3), round(y, 3), round(math.hypot(x, y), 3), fr))
print('(period, radius bin lower edge): present / absent  (bin 16 = r >= 0.4)')
for k in sorted(res): print(k[0], f'{k[1]*0.025:.3f}', res[k][0], '/', res[k][1])
for per, xs in absent_xy.items():
    rs = sorted(a[2] for a in xs)
    print(per, 'absent frames', len(xs), 'max radius among absent', rs[-1] if rs else None, 'radius p50', rs[len(rs)//2] if rs else None)
    print('  sample absent (x,y,r,frame):', xs[:3], xs[-3:])
