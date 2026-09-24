#!/usr/bin/env python3
"""shadow_alpha_casters per-frame rows: field totals, frames with seen>0 / tested>0, per-frame distribution of tested,
and the capture (F8) frames. usage: alpha_casters_rows.py LOG"""
import re, sys, statistics, collections
L = sys.argv[1]; kv = re.compile(r'(\w+)=(\S+)')
tot = collections.Counter(); per = []; ready = collections.Counter(); cap = set()
with open(L, 'rb') as f:
    for raw in f:
        if raw.startswith(b'frame_begin '):
            cap.add(int(re.search(rb'frame=(\d+)', raw).group(1)))
        elif raw.startswith(b'shadow_alpha_casters device='):
            d = dict(kv.findall(raw.decode('latin1'))); fr = int(d['frame'])
            ready[d['ready']] += 1
            vals = {k: int(v) for k, v in d.items() if k not in ('device', 'frame', 'ready')}
            tot.update(vals); per.append((fr, vals))
print('rows', len(per), 'ready', dict(ready)); print('totals', dict(tot))
for k in ('seen', 'tested'):
    xs = sorted(v[k] for _, v in per); nz = [x for x in xs if x]
    print(k, 'frames>0', len(nz), 'mean', round(statistics.mean(xs), 2), 'p50', xs[len(xs)//2], 'p95', xs[int(len(xs)*.95)], 'max', xs[-1])
print('capture frames:')
for fr, v in per:
    if fr in cap: print(' ', fr, ' '.join(f'{k}={x}' for k, x in v.items() if x))
