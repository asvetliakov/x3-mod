"""Per-draw rows for chosen models in chosen frames of a session log (streaming, read-only).
Usage: python3 ods_draws.py LOG FRAMES(comma) MODELS(comma, hex as in census)
Prints: frame draw model lod prims vs ps alpha_tested stage0..3 (identity:WxH:fmt) and PS c5 c6 c9 c10 c11 c12 c13 c17 c21 b0 b1."""
import sys, re, struct
log, frames, models = sys.argv[1], set(sys.argv[2].split(',')), set(sys.argv[3].split(','))
FMT = {'827611204': 'DXT1', '894720068': 'DXT5', '21': 'A8R8G8B8', '861165636': 'DXT3'}
def f4(bits):
    return '(' + ','.join('%.3f' % struct.unpack('<f', bytes.fromhex(b)[::-1])[0] for b in bits.split(',')[:3]) + ')'
cur = None
def flush(c):
    if c and c.get('model') in models:
        st = ' '.join('s%d=%s' % (s, c['tex'].get(s, '-')) for s in range(4))
        pc = ' '.join('c%d=%s' % (r, f4(c['psf'][r]) if r in c['psf'] else '0') for r in (5, 6, 9, 10, 11, 12, 13, 17, 21))
        print(c['frame'], 'draw', c['index'], 'model', c['model'], 'lod', c.get('lod'), 'prims', c['prims'],
              'vs', c['vs'], 'ps', c['ps'], 'alpha_tested', c.get('at'), st, pc, 'b0', c['psb'].get(0), 'b1', c['psb'].get(1))
with open(log, errors='replace') as fh:
    for line in fh:
        t = line[:16]
        if t.startswith('draw '):
            flush(cur); cur = None
            d = dict(re.findall(r'(\w+)=(\S+)', line))
            if d.get('frame') in frames:
                cur = dict(frame=d['frame'], index=d['index'], prims=d.get('primitives'), vs=d.get('vs'), ps=d.get('ps'),
                           tex={}, psf={}, psb={})
            continue
        if cur is None:
            continue
        if t.startswith('object_context'):
            d = dict(re.findall(r'(\w+)=(\S+)', line)); cur['model'] = d.get('model'); cur['lod'] = d.get('lod')
        elif t.startswith('texture stage'):
            d = dict(re.findall(r'(\w+)=(\S+)', line)); cur['tex'][int(d['stage'])] = d['identity']
        elif t.startswith('texture_desc'):
            d = dict(re.findall(r'(\w+)=(\S+)', line)); s = int(d['stage'])
            cur['tex'][s] = '%s:%sx%s:%s' % (cur['tex'].get(s), d['w'], d['h'], FMT.get(d['format'], d['format']))
        elif t.startswith('constant kind=ps'):
            d = dict(re.findall(r'(\w+)=(\S+)', line))
            if d['type'] == 'f': cur['psf'][int(d['reg'])] = d['bits']
            elif d['type'] == 'b': cur['psb'][int(d['reg'])] = d['values']
        elif t.startswith('object_bounds'):
            cur['at'] = dict(re.findall(r'(\w+)=(\S+)', line)).get('alpha_tested')
flush(cur)
