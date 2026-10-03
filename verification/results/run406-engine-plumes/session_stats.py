#!/usr/bin/env python3
"""Run406: s/value histograms of main-jet engine_draw rows (only logged frames), health totals.
usage: session_stats.py <log>"""
import sys, statistics
from collections import Counter
log = sys.argv[1]
sh = Counter(); vh = Counter(); n = 0; uniq = {}
stage_us = []; fso = 0; failed = 0; states = []; end = None; dts = []; prevq = None
for line in open(log, errors='replace'):
    if line.startswith('engine_draw '):
        d = dict(t.split('=', 1) for t in line.split() if '=' in t)
        if int(d['flags'], 16) & 3 or d['body'] == '-1': continue
        n += 1; s = float(d['s']); sh[min(int(s * 10), 9)] += 1; vh[round(float(d['size']), 1)] += 1
        uniq[(int(d['frame']) // 100, d['handle'])] = s
    elif line.startswith('engine_stage '):
        d = dict(t.split('=', 1) for t in line.split() if '=' in t)
        if d['armed'] == '1': stage_us.append(float(d['stage_us']))
    elif line.startswith('engine_frame '):
        d = dict(t.split('=', 1) for t in line.split() if '=' in t); fso += int(d['forwarded_stage_off'])
    elif line.startswith('engine_plumes_failed'): failed += 1
    elif line.startswith('engine_plumes_state'): states.append(line.strip()[:200])
    elif line.startswith('session_end'): end = line.strip()[:300]
print('main-jet rows', n, 's hist (0.1 bins)', [sh[i] for i in range(10)])
us = Counter(min(int(v * 10), 9) for v in uniq.values())
print('unique (burst,handle)', len(uniq), 's hist', [us[i] for i in range(10)])
print('value (size) counts', sorted(vh.items()))
print('stage_us armed n', len(stage_us), 'median', statistics.median(stage_us), 'p99', sorted(stage_us)[int(len(stage_us) * .99)], 'max', max(stage_us))
print('forwarded_stage_off total', fso, 'engine_plumes_failed', failed)
for s in states: print(s)
print(end)
