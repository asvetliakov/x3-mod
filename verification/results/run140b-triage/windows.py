#!/usr/bin/env python3
"""frame_phases windows over time: frame, mean draws of window, dt/pre_render/views/view_submit p50 (us). usage: windows.py RUNDIR"""
import sys, glob, re, statistics as st
KV = re.compile(r'(\w+)=(-?[0-9.]+)(?=\s|$)'); dr = {}; out = []
for raw in open(glob.glob(f'{sys.argv[1]}/session-*.log')[0], 'rb'):
    if raw.startswith(b'frame_end '): x = dict(KV.findall(raw.decode('latin1'))); dr[int(x['frame'])] = int(x['draws'])
    elif raw.startswith(b'frame_phases '): out.append(dict(KV.findall(raw.decode('latin1'))))
for w in out:
    f = int(w['frame']); d = [dr[i] for i in range(f-300, f) if i in dr]
    print(f"{f:6d} draws={st.mean(d) if d else 0:6.1f} dt={w['dt_p50_us']:>6} pre={w['pre_render_p50_us']:>5} views={w['views_p50_us']:>5} submit={w['view_submit_p50_us']:>5} setup={w['view_setup_p50_us']:>5} present={w['present_p50_us']:>5}")
