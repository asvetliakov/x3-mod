#!/usr/bin/env python3
"""Per-class breakdown of one frame from analyze.py draws.jsonl. Usage: breakdown.py <draws.jsonl> <frame> <tol>"""
import json, sys
from collections import Counter, defaultdict
rows = [json.loads(l) for l in open(sys.argv[1])]
fr, tol = int(sys.argv[2]), sys.argv[3]
rows = [r for r in rows if r['frame'] == fr]
cls = defaultdict(Counter)
for r in rows:
    c = cls[r['class']]
    c['draws'] += 1; c['prims'] += r['prims']
    if not r['box']:
        c['no_box'] += 1; continue
    if r.get('offscreen'): c['offscreen'] += 1
    if r.get('near'): c['near'] += 1
    f = r.get('frac', {}).get(tol, 0.0)
    if f >= 1.0:
        c['full'] += 1; c['full_prims'] += r['prims']; c['full_verts'] += r['verts']
        if r.get('blend'): c['full_blend'] += 1
        if r.get('atest') or r.get('alpha_tested'): c['full_atest'] += 1
        if r.get('stale'): c['full_stale'] += 1
        if r.get('area_px', 0) < 16: c['full_lt16px'] += 1
    if f > 0.95: c['mostly'] += 1
keys = ['draws', 'prims', 'no_box', 'offscreen', 'near', 'full', 'full_prims', 'full_verts', 'full_blend', 'full_atest', 'full_stale', 'full_lt16px', 'mostly']
print('class', *keys)
for k in sorted(cls):
    print(k, *[cls[k][x] for x in keys])
# submission order: hull draw indices vs hidden sub-part indices
hull = sorted(r['index'] for r in rows if r['class'] == 'hull' and r['body'] and 'ocelot' in r['body'])
hid = sorted(r['index'] for r in rows if r['class'] in ('turret', 'dock', 'jet', 'other_part') and r.get('frac', {}).get(tol, 0) >= 1.0)
print('ocelot hull indices', hull[:3], '..', hull[-3:] if hull else None, 'hidden sub-part indices min/max', (hid[0], hid[-1]) if hid else None,
      'hidden before first ocelot hull draw', sum(1 for i in hid if hull and i < hull[0]))
print('ocelot s', sorted({r['s'] for r in rows if r['body'] and 'ocelot' in r['body']}))
print('unjoined', Counter((r['prims'], r['box']) for r in rows if r['class'] == 'unjoined'))
