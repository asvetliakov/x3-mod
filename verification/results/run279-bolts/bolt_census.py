#!/usr/bin/env python3
"""Run 75 B (run279): the corvette's bolts as cull-census rows (body v\\00517, model 0x205, radius 784) in the F8 burst
6136-6143, per frame: verdict counts, kept nodes' s/measure/d, the nearest culled_small s/d, the culled_min onset d,
beside the bullet draws (VS 5e484a06 / PS ec1f5c4a) and their primitive counts from the per-draw rows; plus
cull_small_parts_frame. Also tracks one node across frames (d step per frame). Usage: bolt_census.py [LOG]"""
import sys, re, glob, collections
log = sys.argv[1] if len(sys.argv) > 1 else glob.glob('/tmp/x3-bottleX3-run279/session-*.log')[0]
F = range(6136, 6144); kv = lambda s: dict(t.split('=', 1) for t in s.split() if '=' in t)
rows = collections.defaultdict(list); draws = collections.defaultdict(list); small = {}
pat = re.compile(r'^(cull_census device=1|draw device=1|cull_small_parts_frame device=1) frame=(61[34]\d) ')
with open(log, errors='replace') as fh:
    for line in fh:
        m = pat.match(line)
        if not m or int(m.group(2)) not in F: continue
        f = int(m.group(2)); d = kv(line)
        if line.startswith('cull_census') and d.get('body') == 'v\\00517': rows[f].append(d)
        elif line.startswith('draw ') and d.get('vs', '').startswith('5e484a06'): draws[f].append((d['index'], d['primitives'], d['ps'][:8]))
        elif line.startswith('cull_small_parts_frame'): small[f] = d['culled']
track = collections.defaultdict(dict)
for f in F:
    r = rows[f]; c = collections.Counter(x['verdict'] for x in r)
    kept = sorted((int(x['d']), x['s'], x['measure']) for x in r if x['verdict'] == 'kept')
    cs = sorted((int(x['d']), x['s'], x['measure']) for x in r if x['verdict'] == 'culled_small')
    cm = sorted(int(x['d']) for x in r if x['verdict'] == 'culled_min')
    for x in r: track[x['node']][f] = int(x['d'])
    print(f'frame {f} bolts {len(r)} {dict(c)} kept(d,s,measure) {kept} nearest_culled_small {cs[:1]} '
          f'culled_small_d {cs[0][0] if cs else "-"}..{cs[-1][0] if cs else "-"} culled_min_from_d {cm[0] if cm else "-"} '
          f'| bullet_draws(index,prims,ps) {draws[f]} | cull_small_parts culled={small.get(f)}')
steps = [b - a for t in track.values() for a, b in zip([t[f] for f in F if f in t], [t[f] for f in F if f in t][1:])]
steps.sort(); print('per-frame d step of tracked nodes: n', len(steps), 'median', steps[len(steps) // 2] if steps else '-', 'min', steps[:1], 'max', steps[-1:])
