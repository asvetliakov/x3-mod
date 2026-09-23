#!/usr/bin/env python3
"""Run 73 A (run272) per-burst draw accounting by body: joins draw x object_context
(node, model, camera) with cull_census (s, d, radius, lod, thr, body) per frame.
Usage: burst_draws.py LOG OVERLAY_BODIES_TXT FRAME [FRAME...]  (streams the log)."""
import re, sys, collections
log, ovl, frames = sys.argv[1], sys.argv[2], set(sys.argv[3:])
over = set()
for l in open(ovl):
    m = re.match(r'^(\S+): \d+\.cat:', l)
    if m: over.add(m.group(1).lower().replace('/', '\\'))
body = {}; ctx = {}; draws = collections.defaultdict(dict); cen = collections.defaultdict(dict)
CEN = re.compile(r'^cull_census device=\d+ frame=(\d+) view=(\w+) node=(\w+) model=(\w+) s=(-?\d+) measure=(-?\d+) d=(-?\d+) radius=(-?\d+) .*? lod=(-?\d+) verdict=(\w+)(?: scope=\w+)?(?: lods=(\S+) thr=(\S+)(?: body=(\S+))?)?')
for line in open(log, errors='replace'):
    if line.startswith('cull_census '):
        m = CEN.match(line)
        if not m: continue
        if m.group(13): body[m.group(4)] = m.group(13)
        if m.group(1) in frames: cen[m.group(1)][(m.group(2), m.group(3))] = m.groups()
    elif line.startswith('object_context ') or line.startswith('draw '):
        f = re.search(r' frame=(\d+) ', line).group(1)
        if f not in frames: continue
        i = re.search(r' index=(\d+)', line).group(1)
        if line[0] == 'o':
            ctx[(f, i)] = (re.search(r' node=(\w+)', line).group(1), re.search(r' model=(\w+)', line).group(1), re.search(r' camera=(\w+)', line).group(1), re.search(r' lod=(\w+)', line).group(1))
        else:
            draws[f][i] = int(re.search(r'primitives=(\d+)', line).group(1))
for f in sorted(frames, key=int):
    rows = collections.defaultdict(lambda: [0, 0, set(), set(), set()])
    for i, prim in draws[f].items():
        node, model, cam, lod = ctx.get((f, i), ('-', '-', '-', '-'))
        c = cen[f].get((cam, node))
        r = rows[(model, cam)]; r[0] += 1; r[1] += prim
        if c: r[2].add((int(c[4]), int(c[6]), int(c[7]), int(c[8]), c[9], c[10], c[11]))
        else: r[3].add(node)
        r[4].add(int(lod, 16) if lod != '-' else -1)
    cams = collections.Counter(k[1] for k, v in rows.items() for _ in range(v[0]))
    print(f'frame {f} draws={len(draws[f])} by camera={dict(cams)}')
    tot_ovl = 0
    for (model, cam), (n, prim, cs, miss, lods) in sorted(rows.items(), key=lambda kv: -kv[1][0])[:int(__import__("os").environ.get("TOP", "25"))]:
        b = body.get(model, '-'); ino = b.lower() in over
        tot_ovl += n if ino else 0
        ss = sorted(cs)
        desc = ' '.join(f's={s},d={d},r={rad},lod={lo},{v}' for s, d, rad, lo, v, *_ in ss[:3])
        ladder = f'lods={ss[0][5]} thr={ss[0][6]}' if ss else 'no census row'
        print(f' {n:3d} draws {prim:7d} prim model={model} cam={cam[-4:]} ovl={int(ino)} ctxlod={sorted(lods)} {ladder} nodes={len(cs)}+{len(miss)}nocensus {desc} body={b}')
