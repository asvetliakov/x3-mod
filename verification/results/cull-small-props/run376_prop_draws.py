"""Run376 (X3M_CULL_SMALL_PROPS=on, 4 px, F8 frames 3262-3269): per ships\\props\\ draw of one frame, the scope
descriptor (object_context mesh=), node, model, LOD, the VB-extent screen half-side (object_bounds) and the census
radius / s; then how the descriptor values group by body (a per-part descriptor is shared by every node of a body at
the same LOD). Prints counts and a few sample rows only. Usage: run376_prop_draws.py [session.log] [frame]"""
import collections
import glob
import re
import sys

path = sys.argv[1] if len(sys.argv) > 1 else sorted(glob.glob('/tmp/x3-bottleX3-run376/session-*.log'))[0]
FRAME = int(sys.argv[2]) if len(sys.argv) > 2 else 3262
KV = re.compile(r'(\w+)=(\S+)')
census, ctx, bounds, route = {}, {}, {}, {}
norm = lambda n: n.lower().replace('0x', '').zfill(8)
for line in open(path, errors='replace'):
    kind = line.split(' ', 1)[0]
    if kind not in ('cull_census', 'object_bounds', 'object_context', 'motion_route'):
        continue
    if f' frame={FRAME} ' not in line:
        continue
    d = dict(KV.findall(line))
    if kind == 'cull_census':
        census[norm(d['node'])] = d
    elif kind == 'object_context':
        ctx[int(d['index'])] = d
    elif kind == 'object_bounds':
        bounds[int(d['index'])] = 0.5 * max(float(d['sx1']) - float(d['sx0']), float(d['sy1']) - float(d['sy0']))
    else:
        route[int(d['index'])] = d
rows = []
for i, d in sorted(ctx.items()):
    c = census.get(norm(d.get('node', '0')), {})
    b = c.get('body', '')
    if b.lower().startswith('ships\\props\\'):
        rows.append((i, b.split('\\')[-1], norm(d['node']), d.get('mesh'), d.get('model'), d.get('lod'), bounds.get(i),
                     c.get('radius'), c.get('s'), c.get('d'), route.get(i, {}).get('vertex_count'), route.get(i, {}).get('position_type')))
print(f'frame {FRAME}: {len(ctx)} draws, {len(rows)} prop draws, {sum(r[6] is not None for r in rows)} with an object_bounds box, '
      f'{sum(r[6] is not None and r[6] < 4 for r in rows)} under 4 px')
print('index body node descriptor model lod vb_half_px census_radius s d vertex_count position_type')
for r in rows[:12]:
    print(' '.join(str(x) for x in r))
by_body = collections.defaultdict(set)
nodes_by_body = collections.Counter()
for r in rows:
    by_body[(r[1], r[5])].add(r[3])
    nodes_by_body[(r[1], r[5])] += 1
print('descriptors per (body, lod): ' + '; '.join(f'{b}@{lod}: {len(s)} descriptor(s) over {nodes_by_body[(b, lod)]} draws'
                                               for (b, lod), s in sorted(by_body.items())))
span = collections.defaultdict(list)
for r in rows:
    if r[6] is not None:
        span[r[1]].append(r[6])
print('vb half-side px per body (min/max/n): ' + '; '.join(f'{b} {min(v):.2f}/{max(v):.2f}/{len(v)}' for b, v in sorted(span.items())))
