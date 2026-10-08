#!/usr/bin/env python3
"""Combined per-program table from families.json (output of families.py)."""
import json, collections
j = json.load(open('families.json'))
rows = collections.defaultdict(lambda: dict(count=0, first=None, runs=set()))
fam = {f: collections.Counter() for f in ('blend', 'depth', 'raster', 'decl', 'targets', 'topo')}
for run, r in j['runs'].items():
    tag = run.split('/')[2].replace('x3-bottleX3-', '')
    for t in r['table']:
        k = t['key']; key = (k['vs'], k['ps'], json.dumps(k['blend']), json.dumps(k['depth']), json.dumps(k['raster']),
                             len(k['decl']), k['topo'], json.dumps(k['targets']), t['twin'])
        e = rows[key]; e['count'] += t['count']; e['runs'].add(tag)
        e['first'] = e['first'] or f"{tag}:{t['first_frame']}"
        for f in fam: fam[f][json.dumps(k[f])] += t['count']
ids = {f: {v: f[0].upper() + str(i) for i, (v, _) in enumerate(c.most_common())} for f, c in fam.items()}
print('| vs | ps | blend | depth | cull | decl elems | topo | targets | twin row | draws | runs | first |')
print('|---|---|---|---|---|---|---|---|---|---|---|---|')
for key, e in sorted(rows.items(), key=lambda kv: (kv[0][1], -kv[1]['count'])):
    vs, ps, b, d, r, dl, tp, tg, tw = key
    print(f"| {vs[:8]} | {ps[:8]} | {ids['blend'][b]} | {ids['depth'][d]} | {json.loads(r)[0]} | {dl} | {tp} | {ids['targets'][tg]} | {tw} | {e['count']} | {','.join(sorted(e['runs']))} | {e['first']} |")
print()
for f, c in fam.items():
    print(f'## {f}: {len(c)} distinct')
    for v, n in c.most_common(): print(f'{ids[f][v]} draws={n} value={v}')
