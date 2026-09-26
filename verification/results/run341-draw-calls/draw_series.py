#!/usr/bin/env python3
"""Per-session draws= time series from frame_end rows, compressed to 60-frame bins,
with the shadow replay issue count (proxy-native, not in draws=) per bin and the
marker rows (sector, chase transition, capture, loading) that fall in each bin.
Usage: draw_series.py <session.log> [bin]"""
import re, sys, statistics as st
path = sys.argv[1]; B = int(sys.argv[2]) if len(sys.argv) > 2 else 60
fe = {}; issues = {}; marks = {}
rx_f = re.compile(r'frame=(\d+)')
MARK = ('volumetric_fog_sector', 'sector_background', 'chase_transition_event', 'loading_phase', 'lod_switch ', 'camera_state', 'media_cue_enter', 'chase_camera ')
with open(path, errors='replace') as fh:
    for line in fh:
        if line.startswith('frame_end '):
            d = dict(kv.split('=', 1) for kv in line.split()[1:] if '=' in kv)
            fe[int(d['frame'])] = (int(d['draws']), int(d['capture']), int(d['dt_ms']))
        elif line.startswith('shadow_replay_depth '):
            m = re.search(r'frame=(\d+).* issues=(\d+)', line)
            if m: issues[int(m.group(1))] = int(m.group(2))
        elif line.startswith(MARK):
            m = rx_f.search(line)
            k = line.split()[0]
            if m: marks.setdefault(int(m.group(1)) // B, set()).add(k)
frames = sorted(f for f in fe if fe[f][0] > 0)
nz = [fe[f][0] for f in frames]
q = lambda p: sorted(nz)[int(p * (len(nz) - 1))]
print(f'frames_nonzero={len(nz)} p50={q(.5)} p90={q(.9)} max={max(nz)} ge500={sum(x>=500 for x in nz)} in290_310={sum(290<=x<=310 for x in nz)}')
bins = {}
for f in frames: bins.setdefault(f // B, []).append(f)
for b in sorted(bins):
    ds = [fe[f][0] for f in bins[b]]; iss = [issues[f] for f in bins[b] if f in issues]
    cap = sum(fe[f][1] for f in bins[b])
    print(f'{b*B:6d}-{b*B+B-1:<6d} n={len(ds):3d} draws min={min(ds):4d} med={int(st.median(ds)):4d} max={max(ds):4d}'
          f' replay_issues_med={int(st.median(iss)) if iss else "-"} cap={cap} dt_med={int(st.median(fe[f][2] for f in bins[b]))}'
          f' marks={",".join(sorted(marks.get(b, ())))}')
