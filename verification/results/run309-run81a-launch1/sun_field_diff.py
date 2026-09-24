#!/usr/bin/env python3
"""Run309: classify frames by sun NDC radius (camera_state + shadow_replay_sun; see sun_screen.py) into centre (<0.15),
on-screen (0.5..1.0 box) and behind/off; for per-frame row types compare medians of numeric fields and value sets of
text fields between classes; print fields whose centre median differs from both others by >30% or whose text values differ.
usage: sun_field_diff.py LOG [min_frame]"""
import re, sys, math, statistics, collections
L = sys.argv[1]; MINF = int(sys.argv[2]) if len(sys.argv) > 2 else 0
TYPES = [b'hdr_frame', b'sun_shadow_apply_frame', b'sun_shadow_lane_frame', b'volumetric_fog_frame', b'hull_emission_frame',
         b'screen_emission_additive_frame', b'shadow_alpha_casters', b'shadow_replay_depth', b'shadow_replay_sun', b'motion_output_frame',
         b'fade_route_frame', b'thin_vote_frame', b'emission_source_gain_frame', b'frame_end', b'shadow_replay_candidates']
kv = re.compile(r'(\w+)=(\S+)')
cam = {}; sun = {}; rows = collections.defaultdict(dict)
with open(L, 'rb') as f:
    for raw in f:
        t = raw.split(b' ', 1)[0]
        if t == b'camera_state' and raw.startswith(b'camera_state device='):
            d = dict(kv.findall(raw.decode('latin1')))
            if d.get('valid') == '1': cam[int(d['frame'])] = ([[float(d[f'r{i}{j}']) for j in range(3)] for i in range(3)], float(d['p00']), float(d['p11']))
        if t in TYPES and b' device=' in raw[:40]:
            d = dict(kv.findall(raw.decode('latin1')))
            if 'frame' not in d: continue
            fr = int(d['frame'])
            if t == b'shadow_replay_sun' and 'sun' in d: sun[fr] = [float(x) for x in d['sun'].split(',')]
            rows[t.decode()][fr] = d
cls = {}
for fr in set(cam) & set(sun):
    if fr < MINF: continue
    r, p00, p11 = cam[fr]; s = sun[fr]
    v = [sum(s[i]*r[i][j] for i in range(3)) for j in range(3)]
    if v[2] <= 0: cls[fr] = 'behind'; continue
    x = v[0]/v[2]*p00; y = v[1]/v[2]*p11; rad = math.hypot(x, y)
    cls[fr] = 'centre' if rad < 0.15 else ('onscreen' if abs(x) <= 1 and abs(y) <= 1 and rad > 0.5 else ('off' if abs(x) > 1 or abs(y) > 1 else 'mid'))
print('class counts', collections.Counter(cls.values()))
C = ('centre', 'onscreen', 'behind')
for t, per in rows.items():
    fields = collections.defaultdict(lambda: collections.defaultdict(list))
    for fr, d in per.items():
        c = cls.get(fr)
        if c not in C: continue
        for k, val in d.items():
            if k in ('device', 'frame', 'qpc', 'elapsed_ms', 'sun'): continue
            fields[k][c].append(val)
    for k, bycls in fields.items():
        if not all(bycls.get(c) for c in C): continue
        try:
            med = {c: statistics.median(float(x) for x in bycls[c]) for c in C}
            a, b, z = med['centre'], med['onscreen'], med['behind']
            def diff(p, q): return abs(p - q) > 0.3 * max(abs(p), abs(q), 1e-9) and abs(p - q) > 1e-6
            if diff(a, b) and diff(a, z): print(f'{t}.{k} median centre={a:g} onscreen={b:g} behind={z:g} n={[len(bycls[c]) for c in C]}')
        except ValueError:
            sets = {c: collections.Counter(bycls[c]).most_common(3) for c in C}
            if len({tuple(x for x, _ in sets[c]) for c in C}) > 1: print(f'{t}.{k} values {sets}')
