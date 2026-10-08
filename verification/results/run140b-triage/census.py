#!/usr/bin/env python3
"""scene_graph_census numeric rows: n, median/max engine_nodes and tasks; capture frames. usage: census.py RUNDIR..."""
import sys, glob, re, statistics as s
for d in sys.argv[1:]:
    en, ta, cap = [], [], []
    for raw in open(glob.glob(f'{d}/session-*.log')[0], 'rb'):
        if raw.startswith(b'scene_graph_census '):
            x = dict(re.findall(r'(\w+)=(\S+)', raw.decode('latin1')))
            if x['engine_nodes'] != '-': en.append(int(x['engine_nodes'])); ta.append(int(x['tasks']))
        elif raw.startswith(b'frame_end ') and b' capture=0' not in raw: cap.append(int(re.search(rb'frame=(\d+)', raw).group(1)))
    f = lambda v: f'n={len(v)} med={s.median(v)} max={max(v)}' if v else 'n=0'
    print(d, 'engine_nodes', f(en), 'tasks', f(ta), 'capture_frames', cap)
