#!/usr/bin/env python3
"""Per 300-frame gpu_sync_timing window: median_us of chosen passes plus dt median. usage: gpu_windows.py RUN PASS..."""
import re, glob, sys, collections
log = glob.glob(f'/tmp/x3-bottleX3-run{sys.argv[1]}/session-*.log')[0]; P = sys.argv[2:]
w = collections.defaultdict(dict)
for l in open(log, errors='replace'):
    if l.startswith('gpu_sync_timing window='):
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        if d['pass'] in P or d['pass'] == 'scene': w[d['frames']][d['pass']] = (d['median_us'], d['n'], d['dt_median_us'])
print('frames', *P, 'scene', 'dt_median_us')
for fr, d in w.items():
    print(fr, *[d.get(p, ('-',))[0] for p in P], d.get('scene', ('-',))[0], next(iter(d.values()))[2])
