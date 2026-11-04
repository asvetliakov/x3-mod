#!/usr/bin/env python3
"""Per loading_metric flush (time window), sum ops/count/bytes/total_us and mark windows holding a mid-sector hitch.
Usage: loading_windows.py LOG"""
import re, sys, collections
hitch = [8576, 8713, 12123, 12446, 22938, 24646, 24647, 26673, 27631, 28323, 28644, 28649, 30090, 30091, 30095, 31058, 31801, 34487, 34492,
         1805, 1955, 1991, 2035, 2728, 2767, 3436, 10482]
f = 0; win = collections.OrderedDict()
for ln in open(sys.argv[1], errors='replace'):
    if ln.startswith('frame_end '):
        f = int(re.search(r' frame=(\d+)', ln).group(1))
    elif ln.startswith('loading_metric '):
        d = dict(re.findall(r'(\w+)=(\S+)', ln)); w = win.setdefault(f + 1, collections.Counter())
        w['ops'] += 1; w['count'] += int(d['count']); w['bytes'] += int(d['bytes']); w['us'] += float(d['total_us'])
        w['op_' + d['op']] += int(d['count'])
keys = list(win)
quiet = [win[k] for k in keys if k > 1400]
import statistics
print('windows after load:', len(quiet), 'median ops', statistics.median(w['ops'] for w in quiet), 'median bytes', statistics.median(w['bytes'] for w in quiet), 'median us', round(statistics.median(w['us'] for w in quiet)))
for h in hitch:
    nxt = next((k for k in keys if k >= h), None); prv = max((k for k in keys if k < h), default=None)
    w = win[nxt]
    print(f'hitch {h}: window ({prv},{nxt}] ops={w["ops"]} count={w["count"]} bytes={w["bytes"]} us={w["us"]:.0f} ' + ' '.join(f'{k}={v}' for k, v in w.items() if k.startswith('op_')))
