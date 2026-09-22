#!/usr/bin/env python3
"""Median of per-frame proxy costs over explicit frame ranges (capture frames excluded).
usage: range_medians.py <session.log> A-B [A-B ...]"""
import re, sys, statistics as st
kv = re.compile(r'(\w+)=(\S+)')
LOG = sys.argv[1]; R = [tuple(map(int, a.split('-'))) for a in sys.argv[2:]]
F = {'frame_end': ('dt_ms', 'draws', 'capture'), 'shadow_replay_candidates': ('select_us', 'c4', 'footprint_refused4'),
     'shadow_replay_depth': ('us', 'draws'), 'sun_shadow_apply_frame': ('us',), 'shadow_retention_frame': ('us',),
     'hdr_frame': ('writeback_us', 'meter_us', 'readback_us'), 'motion_output_frame': ('taa_run_us', 'taa_draw_us', 'fill_us'),
     'volumetric_fog_frame': ('cpu_us', 'applied')}
per = {}
lo = min(a for a, b in R); hi = max(b for a, b in R)
with open(LOG, errors='replace') as fh:
    for line in fh:
        t = line.split(' ', 1)[0]
        if t not in F or ' frame=' not in line: continue
        d = dict(kv.findall(line)); f = int(d['frame'])
        if lo <= f <= hi:
            r = per.setdefault(f, {})
            for k in F[t]:
                if k in d: r[t.split('_')[0] + '_' + t.split('_')[-1] + '.' + k] = float(d[k])
keys = sorted({k for r in per.values() for k in r} - {'frame_end.capture'})
print('field ' + ' '.join(f'{a}-{b}' for a, b in R))
for k in keys:
    out = []
    for a, b in R:
        v = [per[f][k] for f in range(a, b + 1) if f in per and k in per[f] and per[f].get('frame_end.capture', 0) == 0]
        out.append(f'{st.median(v):.1f}' if v else '-')
    print(k, ' '.join(out))
