#!/usr/bin/env python3
"""Run250: census of draws without an object_bounds row (draw_accounting no_box) per F8 frame.
Streams the session log; prints per frame the no_box draws grouped by route state, programs and model."""
import re, sys
from collections import Counter, defaultdict
LOG = sys.argv[1]; FRAMES = {int(f) for f in sys.argv[2:]}
kv = re.compile(r'(\w+)=(\S+)')
rows = defaultdict(dict)  # (frame,index) -> {type: fields}
keep = ('draw', 'object_bounds', 'object_context', 'motion_route', 'motion_input')
with open(LOG, errors='replace') as fh:
    for line in fh:
        t = line.split(' ', 1)[0]
        if t not in keep or ' frame=' not in line: continue
        d = dict(kv.findall(line))
        try: f = int(d['frame']); i = int(d['index'])
        except (KeyError, ValueError): continue
        if f in FRAMES: rows[(f, i)][t] = d
for f in sorted(FRAMES):
    idx = [k for k in rows if k[0] == f and 'draw' in rows[k]]
    nb = [k for k in idx if 'object_bounds' not in rows[k]]
    print(f'frame {f}: draws={len(idx)} with_box={len(idx)-len(nb)} no_box={len(nb)}')
    c_route = Counter(); c_prog = Counter(); c_model = Counter(); prims = Counter(); c_blk = Counter(); c_state = Counter()
    for k in nb:
        r = rows[k]; dr = r['draw']; mr = r.get('motion_route'); mi = r.get('motion_input', {}); oc = r.get('object_context', {})
        route = 'no_motion_route' if mr is None else f"routed={mr.get('routed')} gate={mr.get('gate')}"
        c_route[route] += 1
        if mr: c_state[f"zwrite={mr.get('zwrite')} blend={mr.get('blend')} src={mr.get('src')} dst={mr.get('dst')} atest={mr.get('atest')}"] += 1
        c_blk[mi.get('blockers', '-')] += 1
        prog = f"vs={dr.get('vs','?')[:8]} ps={dr.get('ps','?')[:8]}"; c_prog[prog] += 1; prims[prog] += int(dr['primitives'])
        c_model[f"model={oc.get('model','-')} lod={oc.get('lod','-')} scoped={oc.get('scoped','-')}"] += 1
    for name, c in (('route', c_route), ('route_state', c_state), ('motion_input.blockers', c_blk), ('programs', c_prog), ('model', c_model)):
        print(f'  {name}:')
        for key, n in c.most_common(12):
            extra = f' prims={prims[key]}' if name == 'programs' else ''
            print(f'    {n:4d} {key}{extra}')
    # with_box draws: route mix for comparison
    wb = Counter(('routed=' + rows[k].get('motion_route', {}).get('routed', '-')) for k in idx if 'object_bounds' in rows[k])
    print('  with_box route mix:', dict(wb))
