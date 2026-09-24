#!/usr/bin/env python3
"""fade_route_frame totals: rows, rows with fade_tested>0 (first/last frame), sums of every numeric field, max
fade_owner_masked, and fade_tested>0 rows inside the F8 capture frames. usage: fade_route_totals.py LOG"""
import re, sys, collections
kv = re.compile(r'(\w+)=(-?\d+)(?=\s|$)'); s = collections.Counter(); mx = collections.Counter(); n = 0; ft = []
for raw in open(sys.argv[1], 'rb'):
    if not raw.startswith(b'fade_route_frame'): continue
    d = {k: int(v) for k, v in kv.findall(raw.decode('latin1'))}; n += 1
    for k, v in d.items():
        if k in ('frame', 'device'): continue
        s[k] += v; mx[k] = max(mx[k], v)
    if d.get('fade_tested', 0) > 0: ft.append(d['frame'])
print('rows', n, 'fade_tested>0 rows', len(ft), 'first/last', ft[:1], ft[-1:])
print('sums', dict(s)); print('max', dict(mx))
print('fade_tested>0 in 2210..2217:', [f for f in ft if 2210 <= f <= 2217])
