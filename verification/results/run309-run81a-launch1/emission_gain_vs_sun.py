#!/usr/bin/env python3
"""emission_source_gain_frame rows (present only on frames with emission-source draws) against the sun NDC radius
(camera_state + shadow_replay_sun) and the projection F: counts per (F period, radius bin) of the row's admitted,
admitted_screen, refused_* values. usage: emission_gain_vs_sun.py LOG"""
import re, sys, math, collections
L = sys.argv[1]; kv = re.compile(r'(\w+)=(\S+)')
cam = {}; sun = {}; eg = {}
with open(L, 'rb') as f:
    for raw in f:
        if raw.startswith(b'camera_state device='):
            d = dict(kv.findall(raw.decode('latin1')))
            if d.get('valid') == '1': cam[int(d['frame'])] = ([[float(d[f'r{i}{j}']) for j in range(3)] for i in range(3)], float(d['p00']), float(d['p11']))
        elif raw.startswith(b'shadow_replay_sun device='):
            d = dict(kv.findall(raw.decode('latin1')))
            if 'sun' in d: sun[int(d['frame'])] = [float(x) for x in d['sun'].split(',')]
        elif raw.startswith(b'emission_source_gain_frame'):
            d = dict(kv.findall(raw.decode('latin1'))); eg[int(d['frame'])] = d
def radius(fr):
    r, p00, p11 = cam[fr]; s = sun[fr]; v = [sum(s[i]*r[i][j] for i in range(3)) for j in range(3)]
    if v[2] <= 0: return None
    return v[0]/v[2]*p00, v[1]/v[2]*p11
tab = collections.Counter(); present = collections.Counter(); allf = collections.Counter()
BINS = [(0, .05), (.05, .15), (.15, .3), (.3, .6), (.6, 1.0), (1.0, 1e9)]
def b(xy):
    if xy is None: return 'behind'
    if abs(xy[0]) > 1 or abs(xy[1]) > 1: return 'offscreen'
    rr = math.hypot(*xy)
    for lo, hi in BINS:
        if lo <= rr < hi: return f'r{lo}-{hi}'
for fr in sorted(set(cam) & set(sun)):
    per = f'p11={cam[fr][2]:.3f}' if fr < 2512 else 'F=100(0x471c)'
    per = 'F=0x3470' if abs(cam[fr][2]-1.7778) < 1e-3 else per
    k = (per, b(radius(fr))); allf[k] += 1
    if fr in eg:
        d = eg[fr]; present[k] += 1
        tab[k + (f"adm={d['admitted']} scr={d['admitted_screen']} ref_scr={d['refused_screen']} ref_blend={d['refused_blend']} ref_state={d['refused_state']}",)] += 1
print('frames with an emission_source_gain_frame row / all frames, per (F period, sun position bin):')
for k in sorted(allf): print(' ', k, present[k], '/', allf[k])
print('field combinations (top per key):')
by = collections.defaultdict(list)
for k, n in tab.items(): by[k[:2]].append((n, k[2]))
for k in sorted(by): print(' ', k, sorted(by[k], reverse=True)[:4])
