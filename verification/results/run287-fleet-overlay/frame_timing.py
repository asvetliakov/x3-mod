#!/usr/bin/env python3
"""frame_timing windows (300 frames each): frame, dt_p50/p95 us, FPS from dt_p50, draws_p50, draw_p50_us.
Marks the window containing the F8 burst frame. Usage: frame_timing.py LOG BURST_FRAME"""
import re, sys
log, bf = sys.argv[1], int(sys.argv[2]); rows = []
for l in open(log, errors='replace'):
    if l.startswith('frame_timing qpc'):
        g = dict(re.findall(r'(\w+)=(-?\d+)', l)); rows.append(g)
for g in rows:
    f = int(g['frame']); mark = ' <== burst' if f - 300 < bf <= f else ''
    print(f"frame={f} dt_p50={g['dt_p50_us']} dt_p95={g['dt_p95_us']} fps_p50={1e6/int(g['dt_p50_us']):.1f} draws_p50={g['draws_p50']} draw_p50_us={g['draw_p50_us']}{mark}")
import statistics as st
d = [int(g['dt_p50_us']) for g in rows[1:]]
print(f'windows={len(rows)} median of dt_p50 (excl. first)={st.median(d)} us; median draws_p50={st.median(int(g["draws_p50"]) for g in rows[1:])}')
