#!/usr/bin/env python3
"""The 300-frame frame_timing / frame_phases window rows, key fields only (us), one line per window.
Usage: window_rows.py <session.log>"""
import re, sys
T = ('frame', 'dt_p50_us', 'dt_p95_us', 'draws_p50', 'draw_p50_us', 'draw_native_p50_us', 'scene_p50_us', 'present_p50_us', 'gap_pre_p50_us', 'gap_draw_p50_us', 'gap_post_p50_us')
P = ('pre_render_p50_us', 'views_p50_us', 'view_submit_p50_us', 'scene_end_p50_us', 'overlays_p50_us', 'present_p50_us')
ph = {}
rows = []
with open(sys.argv[1], errors='replace') as fh:
    for line in fh:
        if line.startswith('frame_timing '):
            d = dict(re.findall(r'(\w+)=(-?[0-9.]+)', line)); rows.append(d)
        elif line.startswith('frame_phases '):
            d = dict(re.findall(r'(\w+)=(-?[0-9.]+)', line)); ph[d['frame']] = d
print(' '.join(T) + ' | ph:' + ' '.join(p.replace('_p50_us', '') for p in P))
for d in rows:
    p = ph.get(d['frame'], {})
    print(' '.join(d.get(k, '-') for k in T) + ' | ' + ' '.join(p.get(k, '-') for k in P))
