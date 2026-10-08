#!/usr/bin/env python3
"""Share of in-flight frames 20-50 ms (qpc dt) per app-draw bin (40 wide), per run; run16 also by tested bin.
usage: tail_by_draws.py RUNDIR..."""
import re, sys, glob
from collections import defaultdict
KV = re.compile(r'(\w+)=(-?[0-9.]+)(?=\s|$)')
for d in sys.argv[1:]:
    fe = {}; oc = {}
    for raw in open(glob.glob(f'{d}/session-*.log')[0], 'rb'):
        k = raw.split(b' ', 1)[0]
        if k in (b'frame_end', b'occlusion_cull'):
            x = {a: float(b) for a, b in KV.findall(raw.decode('latin1'))}; (fe if k == b'frame_end' else oc)[int(x['frame'])] = x
    fs = sorted(fe); L = max(fs, key=lambda f: fe[f]['dt_ms']); lo, hi = L + 10, fs[-1] - 10
    S = [(b, (fe[b]['qpc'] - fe[a]['qpc']) / 1e4) for a, b in zip(fs, fs[1:]) if b == a + 1 and lo <= b <= hi]
    for key, fn in (('draws', lambda f: fe[f]['draws']), ('tested', lambda f: oc[f]['tested'] if f in oc else 0)):
        if key == 'tested' and not oc: continue
        b = defaultdict(lambda: [0, 0])
        for f, x in S:
            c = b[int(fn(f)) // 40 * 40]; c[0] += 1; c[1] += 20 < x <= 50
        print(d.split('-')[-1], f'by {key}:', ' | '.join(f'{k}: {v[1]}/{v[0]} ({100*v[1]/v[0]:.1f}%)' for k, v in sorted(b.items()) if v[0] >= 50))
