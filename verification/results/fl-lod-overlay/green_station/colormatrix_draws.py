"""Per draw of a capture frame: model, lod, prims, ps c1..c3 (colour matrix rows) and c19/c24, flag non-identity.
Usage: colormatrix_draws.py LOG START END [MODELHEX]"""
import sys, struct
L, a, b = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]); want = sys.argv[4] if len(sys.argv) > 4 else None
rows = {}; cur = None
def fl(bits): return [round(struct.unpack('<f', bytes.fromhex(x)[::-1])[0], 3) for x in bits.split(',')]
with open(L, 'rb') as f:
    for n, raw in enumerate(f, 1):
        if n < a: continue
        if n > b: break
        s = raw.decode('latin1').rstrip(); h = s.split(' ', 1)[0]
        if h == 'draw':
            d = dict(t.split('=', 1) for t in s.split()[1:] if '=' in t); cur = int(d['index']); rows[cur] = {'prims': d['primitives'], 'ps': d['ps'][:8]}
        elif h == 'object_context' and cur in rows:
            d = dict(t.split('=', 1) for t in s.split()[1:] if '=' in t); rows[cur].update(model=d['model'], lod=d['lod'])
        elif h == 'constant' and cur in rows and ' kind=ps type=f ' in s:
            d = dict(t.split('=', 1) for t in s.split()[1:])
            if d['reg'] in ('1', '2', '3', '19', '24'): rows[cur]['c' + d['reg']] = fl(d['bits'])
ident = {'c1': [1, 0, 0, 0], 'c2': [0, 1, 0, 0], 'c3': [0, 0, 1, 0]}
for i, r in rows.items():
    if want and r.get('model') != want: continue
    if r.get('model', '00000000') == '00000000': continue
    cm = all(r.get(k) == v for k, v in ident.items())
    print(i, r.get('model'), 'lod=%s' % r.get('lod'), 'prims=%s ps=%s' % (r['prims'], r['ps']), 'matrix=%s' % ('identity' if cm else [r.get(k) for k in ('c1', 'c2', 'c3')]), 'c19=%s c24=%s' % (r.get('c19'), r.get('c24')))
