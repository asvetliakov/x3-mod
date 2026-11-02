#!/usr/bin/env python3
"""FL run 18 triage: per-frame numeric cost fields from x3m.log -> a local pickle (untracked).

Usage: python3 -I extract_frames.py <x3m.log> <out.pkl>
Every row carrying device=1 and frame=N contributes its *_us / *_ms / calls / draws / records / nozzles fields under
'<row>.<field>'. frame_end supplies qpc (dt from consecutive qpc) and elapsed_ms. Rows without frame= (log_writer,
window_pump_gap, telemetry_*, resource, ...) are attached to the last frame_end seen and counted per frame.
"""
import pickle, re, sys
from collections import defaultdict

KEEP = re.compile(r'(_us|_ms|^calls|^draws|^records|^nozzles|^dropped_total|^bytes|^rows|^frames|^missed_locks|'
                  r'^worker_nodes_per_s|^upload_bytes|^applied|^cards|^candidates|casters|^density_scale|^strength|^cascades|^drawn|^nozzles)$')
frames = defaultdict(dict)
counts = defaultdict(lambda: defaultdict(int))
last = 0
qpc_freq = 1e7
with open(sys.argv[1], 'r', errors='replace') as f:
    for line in f:
        sp = line.split(' ', 1)
        kind = sp[0]
        if len(sp) < 2:
            continue
        rest = sp[1]
        if kind == 'frame_end':
            d = dict(t.split('=', 1) for t in rest.split() if '=' in t)
            if d.get('device') != '1':
                continue
            n = int(d['frame']); last = n
            fr = frames[n]
            fr['qpc'] = int(d['qpc']); fr['elapsed_ms'] = int(d['elapsed_ms']); fr['draws'] = int(d['draws'])
            fr['capture'] = int(d['capture'])
            continue
        if ' frame=' in ' ' + rest and 'device=1 ' in rest:
            d = dict(t.split('=', 1) for t in rest.split() if '=' in t)
            try:
                n = int(d.get('frame', ''))
            except ValueError:
                n = last
        else:
            d = dict(t.split('=', 1) for t in rest.split() if '=' in t)
            n = last
            counts[n][kind] += 1
        fr = frames[n]
        for k, v in d.items():
            if KEEP.search(k):
                try:
                    fr[kind + '.' + k] = fr.get(kind + '.' + k, 0.0) + float(v)
                except ValueError:
                    pass
        counts[n]['row:' + kind] += 1
with open(sys.argv[2], 'wb') as o:
    pickle.dump({'frames': dict(frames), 'counts': {k: dict(v) for k, v in counts.items()}}, o)
print('frames', len(frames))
