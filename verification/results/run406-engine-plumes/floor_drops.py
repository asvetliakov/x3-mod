#!/usr/bin/env python3
"""Run406: frames where floored drops while nozzles does not (floor collapse with secondaries still drawn),
whole session and the window; plus faded/capped/culled_small ranges in a window.
usage: floor_drops.py <log> lo hi"""
import sys
log, lo, hi = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
prev = None; drops = []; n = 0; win = {}
for line in open(log, errors='replace'):
    if not line.startswith('engine_stage '): continue
    d = dict(t.split('=', 1) for t in line.split() if '=' in t)
    if d['armed'] != '1': continue
    f, noz, fl = int(d['frame']), int(d['nozzles']), int(d['floored'])
    n += 1
    if lo <= f <= hi:
        for k in ('faded', 'capped', 'culled_small', 'culled_behind', 'culled_idle', 'discs'):
            win.setdefault(k, []).append(int(d[k]))
    if prev and fl < prev[2] and noz >= prev[1]:
        drops.append((f, prev[1], noz, prev[2], fl))
    prev = (f, noz, fl)
print('armed stage rows', n, 'floor drops with nozzles not falling', len(drops))
for x in drops[:40]: print('frame=%d nozzles %d->%d floored %d->%d' % x)
for k, v in win.items(): print(k, 'min', min(v), 'max', max(v), 'nonzero_frames', sum(1 for x in v if x))
