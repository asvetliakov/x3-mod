#!/usr/bin/env python3
"""900-frame windows: dt p50, draws, engine_stage node/stage fields, engine_light ships. usage: engine_windows.py RUNDIR"""
import sys, glob, re, statistics as st, collections as C
KV = re.compile(r'(\w+)=(-?[0-9.]+)(?=\s|$)'); W = C.defaultdict(lambda: C.defaultdict(list))
keys = {b'frame_end': ('dt_ms', 'draws', 'issued'), b'engine_stage': ('node_walk_us', 'node_not_ship', 'node_children', 'node_roots', 'stage_us', 'records', 'ribbons_live'), b'engine_light_frame': ('ships', 'ships_drawn', 'lights')}
for raw in open(glob.glob(f'{sys.argv[1]}/session-*.log')[0], 'rb'):
    k = raw.split(b' ', 1)[0]
    if k in keys:
        x = dict(KV.findall(raw.decode('latin1'))); f = int(x.get('frame', 0))
        if k == b'frame_end' and x.get('capture') != '0': continue
        for kk in keys[k]:
            if kk in x: W[f // 900][kk].append(float(x[kk]))
cols = [c for v in keys.values() for c in v]
print('win ' + ' '.join(cols))
for w in sorted(W):
    print(f'{w*900:6d} ' + ' '.join(f'{st.median(W[w][c]) if W[w][c] else 0:.1f}' for c in cols))
