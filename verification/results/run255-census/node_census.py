"""Run 255: per-node census (s, lod, ladder, body) joined to per-node draws,
alpha-tested draws and pixel-shader set, for one F8 frame per burst.
Usage: python3 node_census.py LOG FRAME [FRAME...]  (streams the log once)."""
import sys, re, collections
log, frames = sys.argv[1], set(sys.argv[2:])
kv = re.compile(r'(\w+)=(\S+)')
cen = {f: {} for f in frames}; draws = {f: collections.defaultdict(lambda: {'n':0,'alpha':0,'ps':collections.Counter(),'aps':collections.Counter(),'lod':set()}) for f in frames}
dps = {}; views = {f: collections.Counter() for f in frames}; ft = []
with open(log, errors='replace') as fh:
    for line in fh:
        k = line[:15]
        if not (k.startswith('cull_census ') or k.startswith('draw ') or k.startswith('object_context') or k.startswith('object_bounds ') or k.startswith('frame_timing ')):
            continue
        if k.startswith('frame_timing '):
            d = dict(kv.findall(line)); ft.append((int(d['frame']), int(d['dt_p50_us']), int(d['draws_p50']))); continue
        m = re.search(r' frame=(\d+) ', line)
        if not m or m.group(1) not in frames: continue
        f = m.group(1); d = dict(kv.findall(line))
        if k.startswith('cull_census '):
            cen[f].setdefault(d['node'], []).append(d); views[f][d['view']] += 1
        elif k.startswith('draw '):
            dps[(f, d['index'])] = d['ps']
        elif k.startswith('object_context'):
            r = draws[f][d['node']]; r['n'] += 1; r['model'] = d['model']; r['lod'].add(int(d['lod'], 16)); r['ps'][dps.get((f, d['index']))] += 1
        elif k.startswith('object_bounds ') and d.get('alpha_tested') == '1':
            r = draws[f][d['node']]; r['alpha'] += 1; r['aps'][dps.get((f, d['index']))] += 1
for f in sorted(frames, key=int):
    tot = sum(r['n'] for r in draws[f].values()); alpha = sum(r['alpha'] for r in draws[f].values())
    print(f'== frame {f} node_draws={tot} alpha_tested={alpha} census_views={dict(views[f].most_common(3))}')
    print('node      model     draws alpha opaque drawn_lod census(s,lod,verdict) lods thr body')
    for node, r in sorted(draws[f].items(), key=lambda x: -x[1]['n']):
        if r['n'] < 2: continue
        c = cen[f].get(node, [])
        cs = ';'.join(f"{x['s']},{x['lod']},{x['verdict']}" for x in c[:3]) or '-'
        c0 = c[0] if c else {}
        print(f"{node} {r['model']} {r['n']:5d} {r['alpha']:5d} {r['n']-r['alpha']:6d} {sorted(r['lod'])} {cs} {c0.get('lods','-')} {c0.get('thr','-')} {c0.get('body','-')}")
    print('alpha-tested ps:', dict(sum((r['aps'] for r in draws[f].values()), collections.Counter()).most_common(8)))
print('== frame_timing windows (frame, dt_p50_us, draws_p50) near bursts')
for t in ft:
    if any(abs(t[0]-int(f)) <= 600 for f in frames): print(t)
