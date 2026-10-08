#!/usr/bin/env python3
"""dt p50 by draws bin, in-flight frames split at frame SPLIT (capture frames excluded). usage: split_by_draws.py RUNDIR LOAD END SPLIT"""
import sys, glob, re, statistics as st, collections as C
d, lo, hi, sp = sys.argv[1], *map(int, sys.argv[2:5]); B = [C.defaultdict(list), C.defaultdict(list)]
for raw in open(glob.glob(f'{d}/session-*.log')[0], 'rb'):
    if raw.startswith(b'frame_end '):
        x = dict(re.findall(r'(\w+)=(\S+)', raw.decode('latin1'))); f = int(x['frame'])
        if lo <= f <= hi and x['capture'] == '0': B[f >= sp][int(x['draws']) // 40 * 40].append(float(x['dt_ms']))
for i, n in enumerate((f'<{sp}', f'>={sp}')):
    print(n, ' | '.join(f'{b}:{len(v)} p50={st.median(v):.0f} mean={st.mean(v):.1f}' for b, v in sorted(B[i].items()) if len(v) >= 50))
