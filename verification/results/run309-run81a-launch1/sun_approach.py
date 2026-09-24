#!/usr/bin/env python3
"""Run309: time series across a sun-centred window: sun NDC radius, camera p11, hdr_frame ev/ev_target/luma_p99/luma_lit/
lit_fraction, sun_shadow_lane non_depth_writers, hull_emission admitted/programs, frame dt.
usage: sun_approach.py LOG FIRST LAST STEP"""
import re, sys, math
L, A, B, S = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
kv = re.compile(r'(\w+)=(\S+)'); want = lambda fr: A <= fr <= B
cam = {}; sun = {}; hdr = {}; lane = {}; hull = {}; fe = {}
with open(L, 'rb') as f:
    for raw in f:
        t = raw.split(b' ', 1)[0]
        if t not in (b'camera_state', b'shadow_replay_sun', b'hdr_frame', b'sun_shadow_lane_frame', b'hull_emission_frame', b'frame_end'): continue
        m = re.search(rb' frame=(\d+)', raw)
        if not m or not want(int(m.group(1))): continue
        d = dict(kv.findall(raw.decode('latin1'))); fr = int(d['frame'])
        if t == b'camera_state' and d.get('valid') == '1': cam[fr] = ([[float(d[f'r{i}{j}']) for j in range(3)] for i in range(3)], float(d['p00']), float(d['p11']))
        elif t == b'shadow_replay_sun' and 'sun' in d: sun[fr] = [float(x) for x in d['sun'].split(',')]
        elif t == b'hdr_frame': hdr[fr] = d
        elif t == b'sun_shadow_lane_frame': lane[fr] = d
        elif t == b'hull_emission_frame': hull[fr] = d
        elif t == b'frame_end': fe[fr] = d
for fr in range(A, B + 1, S):
    if fr not in cam or fr not in sun: print(fr, 'no camera/sun'); continue
    r, p00, p11 = cam[fr]; s = sun[fr]; v = [sum(s[i]*r[i][j] for i in range(3)) for j in range(3)]
    pos = 'behind' if v[2] <= 0 else f'ndc=({v[0]/v[2]*p00:+.2f},{v[1]/v[2]*p11:+.2f})'
    h = hdr.get(fr, {}); l = lane.get(fr, {}); u = hull.get(fr, {})
    print(fr, pos, f"p11={p11:.3f} ev={h.get('ev')} ev_target={h.get('ev_target')} ev_key={h.get('ev_key')} p99={h.get('luma_p99')} luma_lit={h.get('luma_lit')} lit_frac={h.get('lit_fraction')} ndw={l.get('non_depth_writers')} rcv={l.get('receiver_draws')} hull_adm={u.get('admitted')} hull_prog={u.get('programs')} draws={fe.get(fr,{}).get('draws')}")
