"""Per draw in given frames: the state block that FOLLOWS the 'draw' row (the log writes the draw row first,
then geometry/texture/sampler/constant rows for that same draw). Prints model, lod, prims, vs, ps, VB/stride,
textures s0..s6 (type:identity:WxH), and pixel-shader float registers c3..c8, c9..c13.
usage: draw_state.py LOG MODELS|all FRAME...   (MODELS: comma list of model ids)"""
import sys, re, struct
log, models, frames = sys.argv[1], sys.argv[2], set(sys.argv[3:])
models = None if models == 'all' else set(models.split(','))
kv = re.compile(r'(\w+)=(\S+)')
def fl(bits): return ','.join(f"{struct.unpack('<f', struct.pack('<I', int(b, 16)))[0]:.3f}" for b in bits.split(',')[:3])
draws, cur = [], None
tag = tuple(f' frame={x} ' for x in frames)
with open(log, errors='replace') as fh:
    for line in fh:
        if line.startswith('draw '):
            cur = None
            if any(t in line for t in tag):
                d = dict(kv.findall(line)); cur = {'d': d, 'tex': {}, 'c': {}, 'ctx': None}; draws.append(cur)
            continue
        if cur is None: continue
        if line.startswith('object_context'): cur['ctx'] = dict(kv.findall(line))
        elif line.startswith('texture stage='):
            d = dict(kv.findall(line)); cur['tex'][d['stage']] = [d.get('type'), d.get('identity'), '-']
        elif line.startswith('texture_desc'):
            d = dict(kv.findall(line))
            if d['stage'] in cur['tex']: cur['tex'][d['stage']][2] = f"{d['w']}x{d['h']}"
        elif line.startswith('constant kind=ps type=f reg='):
            d = dict(kv.findall(line)); r = int(d['reg'])
            if 3 <= r <= 13: cur['c'][r] = fl(d['bits'])
        elif line.startswith('vertex_buffer '): cur['vb'] = dict(kv.findall(line))
        elif line.startswith('draw_args'): cur['args'] = dict(kv.findall(line))
for x in draws:
    c = x['ctx']
    if c is None or (models is not None and c['model'] not in models): continue
    d = x['d']; t = ' '.join(f"s{s}={':'.join(x['tex'][s])}" for s in sorted(x['tex']))
    cs = ' '.join(f"c{r}=({x['c'].get(r, '0')})" for r in range(3, 14))
    vb = x.get('vb', {}); a = x.get('args', {})
    print('R', d['frame'], d['index'], c['model'], int(c['lod'], 16), c['flags130'], d['primitives'], d['vs'], d['ps'],
          f"vb={vb.get('identity')}:{int(vb.get('bytes', 0)) // 40}v nv={a.get('num_vertices')}", t, cs)
