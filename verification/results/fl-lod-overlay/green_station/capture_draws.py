"""Per-draw summary of one capture frame in an x3m log.
Usage: capture_draws.py LOG START_LINE END_LINE
Prints index, prims, nverts, vs, ps, model, lod, node_handle, mesh, flags, stage0..3 textures (identity:wxh:fmt), key states."""
import sys
log, a, b = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
FMT = {827611204:'DXT1', 894720068:'DXT5', 861165636:'DXT3', 21:'A8R8G8B8', 22:'X8R8G8B8', 50:'L8', 51:'A8L8'}
def kv(line):
    d = {}
    for t in line.split()[1:]:
        if '=' in t:
            k, v = t.split('=', 1); d[k] = v
    return d
draws = []; cur = None; tex = {}
with open(log, 'rb') as f:
    for n, raw in enumerate(f, 1):
        if n < a: continue
        if n > b: break
        line = raw.decode('latin1').rstrip()
        head = line.split(' ', 1)[0]
        if head == 'draw':
            cur = {'d': kv(line), 'tex': {}, 'st': {}}; draws.append(cur)
        elif cur is None: continue
        elif head == 'object_context': cur['ctx'] = kv(line)
        elif head == 'texture':
            d = kv(line); cur['tex'][d['stage']] = [d.get('identity'), d.get('levels')]
        elif head == 'texture_desc':
            d = kv(line); cur['tex'].setdefault(d['stage'], ['?','?']).append('%sx%s:%s' % (d['w'], d['h'], FMT.get(int(d['format']), d['format'])))
        elif head == 'state':
            d = kv(line); cur['st'][d['id']] = d['value']
        elif head == 'draw_args':
            cur['args'] = kv(line)
for c in draws:
    d = c['d']; x = c.get('ctx', {}); s = c['st']
    t = ' '.join('s%s=%s' % (k, ':'.join(str(v) for v in c['tex'][k])) for k in sorted(c['tex']) if int(k) < 4)
    print(d['index'], 'prims=%s nv=%s' % (d['primitives'], c.get('args', {}).get('num_vertices')), 'vs=%s ps=%s' % (d['vs'][:8], d['ps'][:8]),
          'model=%s lod=%s nh=%s mesh=%s f12c=%s' % (x.get('model'), x.get('lod'), x.get('node_handle'), x.get('mesh'), x.get('flags12c')),
          'ab=%s src=%s dst=%s at=%s z=%s zw=%s fog=%s' % (s.get('27'), s.get('19'), s.get('20'), s.get('15'), s.get('7'), s.get('14'), s.get('28')), t)
