"""Run 257: per-node census like run255-census/node_census.py but keeps 1-draw nodes
and also emits per-body totals (draws, alpha, nodes). Usage: node_census.py LOG FRAME..."""
import sys, re, collections
log, frames = sys.argv[1], set(sys.argv[2:])
kv = re.compile(r'(\w+)=(\S+)')
cen = {f: {} for f in frames}; draws = {f: collections.defaultdict(lambda: {'n':0,'alpha':0,'lod':set(),'model':'?'}) for f in frames}
ft = []
with open(log, errors='replace') as fh:
    for line in fh:
        k = line[:15]
        if k.startswith('frame_timing '):
            d = dict(kv.findall(line)); ft.append((int(d['frame']), int(d['dt_p50_us']), int(d['draws_p50']))); continue
        if not (k.startswith('cull_census ') or k.startswith('object_context') or k.startswith('object_bounds ')): continue
        m = re.search(r' frame=(\d+) ', line)
        if not m or m.group(1) not in frames: continue
        f = m.group(1); d = dict(kv.findall(line))
        if k.startswith('cull_census '): cen[f].setdefault(d['node'], []).append(d)
        elif k.startswith('object_context'):
            r = draws[f][d['node']]; r['n'] += 1; r['model'] = d['model']; r['lod'].add(int(d['lod'], 16))
        elif d.get('alpha_tested') == '1': draws[f][d['node']]['alpha'] += 1
for f in sorted(frames, key=int):
    tot = sum(r['n'] for r in draws[f].values()); alpha = sum(r['alpha'] for r in draws[f].values())
    print(f'== frame {f} node_draws={tot} alpha_tested={alpha}')
    body = collections.defaultdict(lambda: [0,0,0])
    for node, r in sorted(draws[f].items(), key=lambda x: -x[1]['n']):
        c = cen[f].get(node, []); c0 = c[0] if c else {}
        b = f"{r['model']} {c0.get('body','-')}"; body[b][0] += r['n']; body[b][1] += r['alpha']; body[b][2] += 1
        cs = ';'.join(f"{x['s']},{x['lod']},{x['verdict']}" for x in c[:2]) or '-'
        print(f"N {node} {r['model']} {r['n']:4d} {r['alpha']:3d} {sorted(r['lod'])} {cs} {c0.get('lods','-')} {c0.get('thr','-')} {c0.get('body','-')}")
    for b, v in sorted(body.items(), key=lambda x: -x[1][0]): print(f"B {v[0]:4d} {v[1]:3d} {v[2]:3d} {b}")
print('== frame_timing')
for t in ft:
    if any(abs(t[0]-int(f)) <= 600 for f in frames): print(t)
