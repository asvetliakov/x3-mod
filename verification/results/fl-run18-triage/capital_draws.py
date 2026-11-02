#!/usr/bin/env python3
"""FL run 18: group capture frame draws by scene node to find the capital ship.
usage: capital_draws.py FILTERED_ROWS [frame] [node]
FILTERED_ROWS = grep -n -E 'frame=1597[012] ' x3m.log | grep -E '^[0-9]+:(draw |object_context|object_bounds|engine_draw|motion_route)'
"""
import sys, re, collections
path = sys.argv[1]; frame = sys.argv[2] if len(sys.argv) > 2 else '15972'
want = sys.argv[3] if len(sys.argv) > 3 else None
kv = lambda s: dict(re.findall(r'(\w+)=(\S+)', s))
draws = collections.defaultdict(dict)
for ln in open(path):
    ln = ln.split(':', 1)[1]
    kind = ln.split(' ', 1)[0]
    d = kv(ln)
    if d.get('frame') != frame or 'index' not in d: continue
    draws[int(d['index'])][kind] = d
nodes = collections.defaultdict(list)
for i, r in sorted(draws.items()):
    oc = r.get('object_context', {})
    nodes[oc.get('node', 'none')].append(i)
if want is None:
    print('node draws prims models flags130 bounds_px(max w x h) zmin')
    rows = []
    for n, idx in nodes.items():
        prims = sum(int(draws[i].get('draw', {}).get('primitives', 0)) for i in idx)
        models = sorted({draws[i].get('object_context', {}).get('model') for i in idx} - {None})
        f130 = sorted({draws[i].get('object_context', {}).get('flags130') for i in idx} - {None})
        w = h = 0; z = 9
        for i in idx:
            b = draws[i].get('object_bounds')
            if b:
                w = max(w, float(b['sx1']) - float(b['sx0'])); h = max(h, float(b['sy1']) - float(b['sy0'])); z = min(z, float(b['zmin']))
        rows.append((w * h, n, len(idx), prims, models, f130, w, h, z))
    for a, n, c, p, m, f, w, h, z in sorted(rows, reverse=True)[:15]:
        print(n, c, p, ','.join(m)[:80], ','.join(f), f'{w:.0f}x{h:.0f}', f'{z:.6f}')
else:
    for i in nodes[want]:
        r = draws[i]; d = r.get('draw', {}); oc = r.get('object_context', {}); b = r.get('object_bounds', {})
        print(i, d.get('vs', '')[:8], d.get('ps', '')[:8], d.get('primitives'), 'depth', oc.get('scope_depth'),
              'model', oc.get('model'), 'f12c', oc.get('flags12c'), 'f130', oc.get('flags130'),
              'bounds', b.get('sx0'), b.get('sy0'), b.get('sx1'), b.get('sy1'), b.get('zmin'),
              'engine_draw' if 'engine_draw' in r else '')
