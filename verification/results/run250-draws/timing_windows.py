#!/usr/bin/env python3
"""Run250: frame_timing / frame_phases p50 fields for the 300-frame windows around the two F8 bursts."""
import re, sys
LOG = sys.argv[1]; WANT = {int(x) for x in sys.argv[2:]}
kv = re.compile(r'(\w+)=(\S+)')
T = ('dt_p50_us', 'dt_p95_us', 'draws_p50', 'draw_p50_us', 'draw_native_p50_us', 'scene_p50_us', 'state_calls_p50', 'present_p50_us')
P = ('dt_p50_us', 'pre_render_p50_us', 'begin_scene_p50_us', 'views_p50_us', 'view_setup_p50_us', 'view_submit_p50_us', 'scene_end_p50_us', 'present_p50_us', 'views_p50', 'incomplete')
with open(LOG, errors='replace') as fh:
    for line in fh:
        t = line.split(' ', 1)[0]
        if t not in ('frame_timing', 'frame_phases'): continue
        d = dict(kv.findall(line))
        if int(d['frame']) not in WANT: continue
        print(t, 'frame=' + d['frame'], ' '.join(f'{k}={d.get(k, "-")}' for k in (T if t == 'frame_timing' else P)))
