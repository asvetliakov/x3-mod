#!/usr/bin/env python3
"""Run 217 outpost triage: per (frame,node,model,lod,ps) motion_route aggregate. Usage: routes.py frames.txt [node...]"""
import sys, collections
rows = collections.defaultdict(lambda: collections.Counter()); meta = {}
for l in open(sys.argv[1]):
    if not l.startswith('motion_route '): continue
    d = dict(t.split('=', 1) for t in l.split()[1:] if '=' in t)
    if d['node'] == '00000000': continue
    if len(sys.argv) > 2 and d['node'] not in sys.argv[2:]: continue
    k = (d['frame'], d['node'], d['model'], d['lod'], d['ps'], d['routed'], d['gate'], d['zwrite'], d['blend'], d['src'], d['dst'], d['fade_arm'], d.get('overlay_arm', '-'), d.get('unmatched', '-'))
    rows[k]['n'] += 1; rows[k]['prim'] += int(d['primitives'])
    meta[k] = d.get('fade_permille')
print('frame node model lod ps routed gate zw blend src dst fade_arm overlay_arm unmatched | draws prims permille')
for k in sorted(rows, key=lambda k: (k[0], -rows[k]['prim'])):
    print(*k, '|', rows[k]['n'], rows[k]['prim'], meta[k])
