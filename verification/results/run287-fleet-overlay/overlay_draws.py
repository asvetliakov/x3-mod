#!/usr/bin/env python3
"""Run 77 A (run287): per-body draw accounting in the F8 burst frames, tagged with overlay slot
(05/06 from <game>/addon/0N.x3m-lod.json). Joins draw x object_context (model, lod, node, camera)
with cull_census (body, lods, thr). A body 'drew merged' when a draw has ctx lod == marker new_lod
and the census ladder length equals len(marker thresholds). Also reports distinct overlay bodies
seen in any cull_census row (the whole log), by slot.
Usage: overlay_draws.py LOG ADDON_DIR"""
import re, sys, json, collections
log, addon = sys.argv[1], sys.argv[2]
mk = {}
for s in ('05', '06'):
    for b in json.load(open(f'{addon}/{s}.x3m-lod.json'))['bodies']:
        mk[b['name'].lower().replace('\\', '/')] = (s, b['new_lod'], b['draws'], len(b['thresholds']), b['thresholds'][1:])
norm = lambda b: b.lower().replace('\\', '/')
CEN = re.compile(r'^cull_census device=\d+ frame=(\d+) view=(\w+) node=(\w+) model=(\w+) .*? lod=(-?\d+) verdict=(\w+)(?: scope=\w+)?(?: lods=(\S+) thr=(\S+)(?: body=(\S+))?)?')
body = {}; ladder = {}; ctx = {}; drawf = collections.defaultdict(dict)
seen = collections.defaultdict(set)   # slot -> bodies in census (any frame)
seen_ladder_ok = collections.defaultdict(set)
for line in open(log, errors='replace'):
    if line.startswith('cull_census '):
        m = CEN.match(line)
        if not m or not m.group(9): continue
        body[m.group(4)] = m.group(9); ladder[m.group(4)] = (m.group(7), m.group(8))
        n = norm(m.group(9))
        if n in mk:
            seen[mk[n][0]].add(n)
            if m.group(7).isdigit() and int(m.group(7)) == mk[n][3]: seen_ladder_ok[mk[n][0]].add(n)
    elif line.startswith('object_context '):
        f = re.search(r' frame=(\d+) ', line).group(1); i = re.search(r' index=(\d+)', line).group(1)
        ctx[(f, i)] = (re.search(r' model=(\w+)', line).group(1), int(re.search(r' lod=(\w+)', line).group(1), 16))
    elif line.startswith('draw '):
        f = re.search(r' frame=(\d+) ', line).group(1); i = re.search(r' index=(\d+)', line).group(1)
        drawf[f][i] = int(re.search(r'primitives=(\d+)', line).group(1))
frames = sorted(drawf, key=int)
per = collections.defaultdict(lambda: collections.Counter())
for f in frames:
    for i in drawf[f]:
        model, lod = ctx.get((f, i), ('-', -1)); b = body.get(model, '-')
        per[(norm(b), model, lod)][f] += 1
print(f'burst frames {frames[0]}..{frames[-1]} ({len(frames)}), draws/frame {sorted(set(len(drawf[f]) for f in frames))}')
tot = collections.Counter()
for (b, model, lod), c in sorted(per.items(), key=lambda kv: (mk.get(kv[0][0], ('zz',))[0], kv[0][0])):
    if b not in mk: tot['non-overlay'] += sum(c.values()); continue
    s, nl, nd, nlad, thr = mk[b]; dpf = sorted(set(c.values()))
    merged = lod == nl and ladder[model][0] == str(nlad)
    tot[f'slot{s}'] += sum(c.values())
    print(f'slot{s} {"MERGED" if merged else "other "} lod={lod} draws/frame={dpf} marker_draws={nd} census lods={ladder[model][0]} thr={ladder[model][1]} marker_thr=*,{",".join(map(str,thr))} {b}')
print('burst draw totals over all frames:', dict(tot))
for s in ('05', '06'):
    print(f'slot{s}: distinct overlay bodies in cull_census (whole log) {len(seen[s])}, with overlay-length ladder {len(seen_ladder_ok[s])}')
