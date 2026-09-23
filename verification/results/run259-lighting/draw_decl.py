"""Per-draw vs/ps/declaration/stride/prims/vertex_count/lightmap-widen for draws of given models in given frames.
usage: draw_decl.py LOG MODELS FRAME... ; prints D rows and S summary rows per (model, lod, vs, ps, decl, stride)."""
import sys, re, collections
log, models, frames = sys.argv[1], set(sys.argv[2].split(',')), set(sys.argv[3:])
kv = re.compile(r'(\w+)=(\S+)'); fr = re.compile(r' frame=(\d+) ')
ctx, route, draw, widen = {}, {}, {}, {}
keys = ('draw ', 'object_context', 'motion_route ', 'hull_lightmap_w')
with open(log, errors='replace') as fh:
    for line in fh:
        k = line[:15]
        if not k.startswith(keys): continue
        m = fr.search(line)
        if not m or m.group(1) not in frames: continue
        d = dict(kv.findall(line))
        if 'index' not in d: continue
        key = (m.group(1), d['index'])
        if k.startswith('draw '): draw[key] = d
        elif k.startswith('object_context'):
            if d['model'] in models: ctx[key] = d
        elif k.startswith('motion_route'): route[key] = d
        else: widen[key] = d
summ = collections.Counter()
for key, c in sorted(ctx.items(), key=lambda x: (int(x[0][0]), int(x[0][1]))):
    d, r, w = draw.get(key, {}), route.get(key, {}), widen.get(key)
    wl = f"widen={w['stage']}:{w['size']}" if w else 'widen=-'
    print('D', key[0], key[1], c['model'], c['node'], int(c['lod'], 16), c['flags130'], d.get('vs'), d.get('ps'), r.get('declaration'), r.get('stride'), d.get('primitives'), r.get('vertex_count'), wl)
    summ[(c['model'], int(c['lod'], 16), c['flags130'], d.get('vs'), d.get('ps'), r.get('declaration'), r.get('stride'), 'widen' if w else 'nowiden')] += 1
for k, v in sorted(summ.items()): print('S', v, *k)
