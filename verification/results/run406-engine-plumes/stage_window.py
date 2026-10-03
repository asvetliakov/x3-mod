#!/usr/bin/env python3
"""Run406: engine_stage nozzles/ships/floored/view_rule per frame in [lo,hi]; prints change points and summary.
usage: stage_window.py <session.log> lo hi"""
import sys
from collections import Counter
log, lo, hi = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
rows = []
for line in open(log, errors='replace'):
    if not line.startswith('engine_stage '): continue
    d = dict(t.split('=', 1) for t in line.split() if '=' in t)
    f = int(d['frame'])
    if lo <= f <= hi:
        rows.append((f, int(d['records']), int(d['nozzles']), int(d['ships']), int(d['floored']), d['view_rule'], d['skipped_other_view']))
prev = None
for r in rows:
    if prev is None or r[1:] != prev[1:]:
        print('frame=%d records=%d nozzles=%d ships=%d floored=%d view_rule=%s skipped=%s' % r)
    prev = r
print('frames', len(rows), 'floored', sorted(Counter(r[4] for r in rows).items()), 'ships', sorted(Counter(r[3] for r in rows).items()))
