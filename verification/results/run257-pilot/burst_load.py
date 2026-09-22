#!/usr/bin/env python3
"""Per captured frame: scene draws and primitives, the 8759c7838bbc86c2 (coarse record) share,
shadow replay casters and their primitives, object_bounds rows, and frame_end dt.
usage: burst_load.py <session.log> <frame> [frame ...]"""
import re, sys
from collections import defaultdict
kv = re.compile(r'(\w+)=(\S+)'); LOG = sys.argv[1]; W = {int(x) for x in sys.argv[2:]}
S = defaultdict(lambda: defaultdict(float))
with open(LOG, errors='replace') as fh:
    for line in fh:
        t = line.split(' ', 1)[0]
        if t not in ('draw', 'shadow_replay_caster', 'object_bounds', 'frame_end', 'shadow_replay_depth'): continue
        m = re.search(r' frame=(\d+)', line)
        if not m or int(m.group(1)) not in W: continue
        d = dict(kv.findall(line)); s = S[int(m.group(1))]
        if t == 'draw':
            p = float(d.get('primitives', 0)); s['draws'] += 1; s['prims'] += p; s['max_prims'] = max(s['max_prims'], p)
            if d.get('ps') == '8759c7838bbc86c2': s['c_draws'] += 1; s['c_prims'] += p
        elif t == 'shadow_replay_caster': s['casters'] += 1; s['caster_prims'] += float(d.get('primitives', 0))
        elif t == 'object_bounds': s['bounds'] += 1
        elif t == 'shadow_replay_depth': s['replay_us'] = float(d['us']); s['replay_draws'] = float(d['draws'])
        elif t == 'frame_end': s['dt_ms'] = float(d['dt_ms'])
K = ['dt_ms', 'draws', 'prims', 'max_prims', 'c_draws', 'c_prims', 'casters', 'caster_prims', 'replay_draws', 'replay_us', 'bounds']
print('frame ' + ' '.join(K))
for f in sorted(S): print(f, ' '.join(f'{S[f][k]:.0f}' for k in K))
