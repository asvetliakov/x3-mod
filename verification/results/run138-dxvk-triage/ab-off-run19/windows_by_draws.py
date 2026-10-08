#!/usr/bin/env python3
"""frame_phases windows (300 frames, in flight) with the window's mean app draws / issued, sorted by draws; plus
in-flight frame counts 20-33 and 33-50 ms (qpc dt).  usage: windows_by_draws.py RUNDIR..."""
import re, sys, glob, statistics as st
KV = re.compile(r'(\w+)=(-?[0-9.]+)(?=\s|$)')
for d in sys.argv[1:]:
    fe = {}; fp = []
    for raw in open(glob.glob(f'{d}/session-*.log')[0], 'rb'):
        k = raw.split(b' ', 1)[0]
        if k == b'frame_end': x = {a: float(b) for a, b in KV.findall(raw.decode('latin1'))}; fe[int(x['frame'])] = x
        elif k == b'frame_phases': fp.append({a: float(b) for a, b in KV.findall(raw.decode('latin1'))})
    fs = sorted(fe); L = max(fs, key=lambda f: fe[f]['dt_ms']); lo, hi = L + 10, fs[-1] - 10
    dq = [(fe[b]['qpc'] - fe[a]['qpc']) / 1e4 for a, b in zip(fs, fs[1:]) if b == a + 1 and lo <= b <= hi]
    print(f'{d}: 20-33ms={sum(20 < x <= 33 for x in dq)} 33-50ms={sum(33 < x <= 50 for x in dq)} >50ms={sum(x > 50 for x in dq)} of {len(dq)}')
    rows = []
    for w in fp:
        e = int(w['frame']); fr = [f for f in range(e - 299, e + 1) if f in fe and lo <= f <= hi]
        if len(fr) >= 250: rows.append((st.mean(fe[f]['draws'] for f in fr), st.mean(fe[f]['issued'] for f in fr), e, w['view_submit_p50_us'], w['views_p50_us'], w['dt_p50_us'], w['present_p50_us']))
    for r in sorted(rows):
        if r[0] >= 195: print('  draws=%5.1f issued=%5.1f frame=%5d view_submit_p50=%5.0f views_p50=%5.0f dt_p50=%5.0f present_p50=%3.0f' % r)
