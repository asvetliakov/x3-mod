"""Texture identities per stage (block rows preceding each draw row) for draws of given models in one frame."""
import sys, re, collections
log, frame, models = sys.argv[1], sys.argv[2], set(sys.argv[3].split(','))
kv = re.compile(r'(\w+)=(\S+)'); tex = []; blk = {}; ctx = {}; ps = {}
tag = f' frame={frame} '
with open(log, errors='replace') as fh:
    for line in fh:
        if line.startswith('texture stage='):
            d = dict(kv.findall(line)); tex.append([d['stage'], d.get('identity'), None])
        elif line.startswith('texture_desc') and tex: tex[-1][2] = dict(kv.findall(line)).get('w') + 'x' + dict(kv.findall(line)).get('h')
        elif line.startswith('draw ') and tag in line:
            d = dict(kv.findall(line)); blk[d['index']] = tex; ps[d['index']] = d['ps']; tex = []
        elif line.startswith('draw '): tex = []
        elif line.startswith('object_context') and tag in line:
            d = dict(kv.findall(line))
            if d['model'] in models: ctx[d['index']] = (d['model'], d['lod'])
agg = collections.Counter()
for i, (m, lod) in sorted(ctx.items(), key=lambda x: int(x[0])):
    s = ' '.join(f"s{a}:{b}:{c}" for a, b, c in blk.get(i, []))
    print('T', m, lod, i, ps[i], s)
    for a, b, c in blk.get(i, []): agg[(m, a, c)] += 1
for k, v in sorted(agg.items()): print('A', k, v)
