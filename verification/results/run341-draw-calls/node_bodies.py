#!/usr/bin/env python3
"""Opaque game draws per scene node in one captured frame, joined to the cull_census row of the same
frame (body, lod, lods, s, verdict); plus the shadow replay caster rows of that frame.
Usage: node_bodies.py <session.log> <frame>"""
import sys, collections, re
path, frame = sys.argv[1], sys.argv[2]
tag = f' frame={frame} '
draws = collections.Counter(); body = {}; cur = None; states = {}; census = collections.Counter()
casters = []; retention = []
def kv(line): return dict(p.split('=', 1) for p in line.split()[1:] if '=' in p)
with open(path, errors='replace') as fh:
    for line in fh:
        if tag not in line and not line.startswith(('state ', 'object_context')): 
            if line.startswith('draw '): cur = None
            continue
        k = line[:line.find(' ')]
        if k == 'draw':
            cur = kv(line) if tag in line else None; states = {}
        elif k == 'object_context' and cur is not None and tag in line:
            d = kv(line)
            if d.get('scoped') == '1': cur['node'] = d['node']; cur['lod'] = d['lod']
        elif k == 'motion_route' and cur is not None:
            d = kv(line)
            if d.get('routed') == '1' and 'node' in cur: draws[(cur['node'], cur['lod'])] += 1
        elif k == 'cull_census':
            d = kv(line); census[d['verdict']] += 1
            body[d['node']] = (d.get('body', '?').split('\\')[-1], d['lod'], d['lods'], d['s'], d['verdict'])
        elif k == 'shadow_replay_caster':
            casters.append(kv(line))
        elif k == 'shadow_retention_caster':
            retention.append(kv(line))
tot = sum(draws.values())
print(f'frame={frame} routed_opaque_draws={tot} nodes={len(draws)} census_verdicts={dict(census)}')
per_node = collections.Counter()
for (n, l), v in draws.items(): per_node[n] += v
hist = collections.Counter(min(v, 10) for v in per_node.values())
print('draws_per_node_histogram(10=10+)', dict(sorted(hist.items())))
for n, v in per_node.most_common(12):
    print(f'  node={n} draws={v} body={body.get(n, ("?",))}')
lod_draws = collections.Counter()
for (n, l), v in draws.items(): lod_draws['lod0' if l == '00000000' else 'lod>0'] += v
print('routed_opaque_by_object_lod', dict(lod_draws))
masks = collections.Counter(); verdicts = collections.Counter(); retained = collections.Counter()
for c in casters:
    m = int(c.get('cascades', '0')); masks[bin(m).count('1')] += 1; verdicts[c.get('verdict')] += 1; retained[c.get('retained')] += 1
print(f'shadow_replay_caster rows={len(casters)} cascades_per_caster={dict(sorted(masks.items()))} verdicts={dict(verdicts)} retained={dict(retained)}'
      f' issues_sum={sum(bin(int(c.get("cascades","0"))).count("1") for c in casters)}')
rc = collections.Counter((r.get('class'), r.get('unseen'), r.get('in_frustum'), 'c0' if r.get('cascades') == '0' else 'c>0') for r in retention)
print(f'shadow_retention_caster rows={len(retention)} (class,unseen,in_frustum,cascades)={dict(rc)}')
