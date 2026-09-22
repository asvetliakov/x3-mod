#!/usr/bin/env python3
"""Run 69 A frame-time rise: per-300-frame-window medians of per-frame proxy costs
next to the frame_timing / frame_phases window p50 rows.
usage: phase_windows.py <session.log> [window=300]"""
import re, sys, statistics as st
from collections import defaultdict
LOG = sys.argv[1]; W = int(sys.argv[2]) if len(sys.argv) > 2 else 300
kv = re.compile(r'(\w+)=(\S+)')
PER = {  # row type -> fields (per frame)
 'frame_end': ('dt_ms', 'draws', 'capture'),
 'shadow_replay_candidates': ('select_us', 'c4', 'footprint_refused4', 'routed'),
 'shadow_replay_depth': ('us', 'draws'),
 'sun_shadow_apply_frame': ('us',),
 'shadow_retention_frame': ('us', 'walk_us'),
 'hdr_frame': ('writeback_us', 'meter_us', 'readback_us'),
 'motion_output_frame': ('taa_run_us', 'fill_us'),
 'volumetric_fog_frame': ('cpu_us', 'applied'),
}
WIN_T = ('dt_p50_us', 'draws_p50', 'draw_p50_us', 'draw_native_p50_us', 'gap_pre_p50_us', 'gap_draw_p50_us', 'gap_post_p50_us', 'state_calls_p50', 'present_p50_us')
WIN_P = ('dt_p50_us', 'pre_render_p50_us', 'begin_scene_p50_us', 'views_p50_us', 'view_setup_p50_us', 'view_submit_p50_us', 'scene_end_p50_us', 'present_p50_us', 'views_p50')
per = defaultdict(dict); win = defaultdict(dict)
with open(LOG, errors='replace') as fh:
    for line in fh:
        t = line.split(' ', 1)[0]
        if t in PER:
            d = dict(kv.findall(line))
            if 'frame' not in d: continue
            f = int(d['frame'])
            for k in PER[t]:
                if k in d:
                    try: per[f][t + '.' + k] = float(d[k])
                    except ValueError: pass
        elif t in ('frame_timing', 'frame_phases'):
            d = dict(kv.findall(line)); f = int(d['frame'])
            for k in (WIN_T if t == 'frame_timing' else WIN_P):
                win[f][t[6:] + '.' + k] = d.get(k, '-')
cols = ['frame_end.dt_ms', 'frame_end.draws', 'shadow_replay_candidates.select_us', 'shadow_replay_candidates.c4',
        'shadow_replay_candidates.footprint_refused4', 'shadow_replay_depth.us', 'sun_shadow_apply_frame.us',
        'shadow_retention_frame.us', 'hdr_frame.writeback_us', 'hdr_frame.meter_us', 'hdr_frame.readback_us',
        'motion_output_frame.taa_run_us', 'motion_output_frame.fill_us', 'volumetric_fog_frame.cpu_us', 'volumetric_fog_frame.applied']
short = ['dt_ms', 'draws', 'sel_us', 'c4', 'fpr4', 'sdepth', 'sunapp', 'retain', 'hdr_wb', 'meter', 'hdr_rb', 'taa', 'fill', 'fog', 'fogon']
print('# per-window medians of per-frame rows (window end frame); fpr4 = mean')
print('win_end nfr ' + ' '.join(short) + ' proxy_sum_ms')
frames = sorted(per)
for end in range(W, frames[-1] + W, W):
    rows = [per[f] for f in range(end - W + 1, end + 1) if f in per and per[f].get('frame_end.capture', 0) == 0]
    if not rows: continue
    out = []; psum = 0.0
    for c in cols:
        v = [r[c] for r in rows if c in r]
        if not v: out.append('-'); continue
        m = st.mean(v) if c.endswith('footprint_refused4') else st.median(v)
        out.append(f'{m:.2f}' if c.endswith('dt_ms') or c.endswith('4') else f'{m:.0f}')
        if c in ('shadow_replay_candidates.select_us', 'shadow_replay_depth.us', 'sun_shadow_apply_frame.us', 'shadow_retention_frame.us',
                 'hdr_frame.writeback_us', 'hdr_frame.meter_us', 'hdr_frame.readback_us', 'motion_output_frame.taa_run_us',
                 'motion_output_frame.fill_us', 'volumetric_fog_frame.cpu_us'):
            psum += m
    print(end, len(rows), ' '.join(out), f'{psum/1000:.2f}')
print('# frame_timing / frame_phases window rows')
for f in sorted(win):
    print(f, ' '.join(f'{k.split(".",1)[0]}.{k.split(".",1)[1].replace("_p50","").replace("_us","")}={v}' for k, v in win[f].items()))
