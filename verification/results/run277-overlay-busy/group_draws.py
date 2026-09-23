#!/usr/bin/env python3
"""Run 74 A (run277): per-frame draw groups. Main view = the frame's most frequent camera.
Main-view draws are classed by their node's cull_census row (same frame, camera, node) and the
overlay body list: overlay_merged (overlay body, object_context lod >= 1), overlay_lod0,
laddered (non-overlay, lods > 1), single_lod (lods == 1), no_census; other views = every other
camera. Also lists main-view nodes drawing > 10 draws.
Usage: group_draws.py LOG OVERLAY_BODIES_TXT FRAME [FRAME...]"""
import re, sys, collections
log, ovl, frames = sys.argv[1], sys.argv[2], set(sys.argv[3:])
over = set()
for l in open(ovl):
    m = re.match(r'^(\S+): \d+\.cat:', l)
    if m: over.add(m.group(1).lower().replace('/', '\\'))
CEN = re.compile(r'^cull_census device=\d+ frame=(\d+) view=(\w+) node=(\w+) model=(\w+) s=(-?\d+) .*? lod=(-?\d+) verdict=(\w+)(?: scope=\w+)?(?: lods=(\S+) thr=(\S+)(?: body=(\S+))?)?')
cen = collections.defaultdict(dict); ctx = {}; draws = collections.defaultdict(dict)
for line in open(log, errors='replace'):
    if line.startswith('cull_census '):
        m = CEN.match(line)
        if m and m.group(1) in frames: cen[m.group(1)][(m.group(2), m.group(3))] = m
    elif line.startswith('object_context ') or line.startswith('draw '):
        f = re.search(r' frame=(\d+) ', line).group(1)
        if f not in frames: continue
        i = re.search(r' index=(\d+)', line).group(1)
        if line[0] == 'o':
            ctx[(f, i)] = (re.search(r' node=(\w+)', line).group(1), re.search(r' camera=(\w+)', line).group(1), int(re.search(r' lod=(\w+)', line).group(1), 16))
        else:
            draws[f][i] = int(re.search(r'primitives=(\d+)', line).group(1))
for f in sorted(frames, key=int):
    cams = collections.Counter(ctx.get((f, i), ('-', '-', 0))[1] for i in draws[f])
    main = cams.most_common(1)[0][0]
    g = collections.Counter(); gp = collections.Counter(); per = collections.Counter(); pbody = {}
    for i, prim in draws[f].items():
        node, cam, lod = ctx.get((f, i), ('-', '-', 0))
        if cam != main: k = 'other_views'
        else:
            c = cen[f].get((cam, node))
            if not c or not c.group(8): k = 'no_census'
            else:
                body = (c.group(10) or '-').lower(); pbody[node] = (c.group(10), c.group(5), c.group(8), c.group(9), lod)
                per[node] += 1
                if body in over: k = 'overlay_merged' if lod >= 1 else 'overlay_lod0'
                elif int(c.group(8)) > 1: k = 'laddered'
                else: k = 'single_lod'
        g[k] += 1; gp[k] += prim
    print(f'frame {f} draws={len(draws[f])} main={main} ' + ' '.join(f'{k}={g[k]}/{gp[k]}p' for k in ('overlay_merged', 'overlay_lod0', 'laddered', 'single_lod', 'no_census', 'other_views')))
    for node, n in per.most_common():
        if n <= 10: break
        b, s, lods, thr, lod = pbody[node]
        print(f'   >10: node={node} draws={n} s={s} lod={lod} lods={lods} thr={thr} body={b} ovl={int((b or "").lower() in over)}')
