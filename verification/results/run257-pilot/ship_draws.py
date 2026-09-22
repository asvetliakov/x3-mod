"""Per-draw rows (index, prims, ps, alpha_tested, blend if logged) for nodes of given models in one frame."""
import sys, re, collections
log, frame, models = sys.argv[1], sys.argv[2], set(sys.argv[3].split(','))
kv = re.compile(r'(\w+)=(\S+)'); draw = {}; ctx = {}; alpha = set()
tag = f' frame={frame} '
with open(log, errors='replace') as fh:
    for line in fh:
        if tag not in line: continue
        k = line[:15]
        if k.startswith('draw '): d = dict(kv.findall(line)); draw[d['index']] = d
        elif k.startswith('object_context'):
            d = dict(kv.findall(line))
            if d['model'] in models: ctx[d['index']] = d
        elif k.startswith('object_bounds ') and 'alpha_tested=1' in line: alpha.add(dict(kv.findall(line))['index'])
per = collections.defaultdict(collections.Counter)
for i, c in sorted(ctx.items(), key=lambda x: int(x[0])):
    d = draw.get(i, {}); per[(c['model'], c['node'])][(d.get('ps'), i in alpha)] += 1
    print('D', c['model'], c['node'], i, c['lod'], d.get('primitives'), d.get('ps'), int(i in alpha))
for k, v in per.items(): print('S', k, dict(v))
