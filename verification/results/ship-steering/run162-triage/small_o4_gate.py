"""Run161 Q4: small ships traced on order 4 with a gate target within 6 r_gate: how many, their game s there, and how many met 0x12
within the band (normaliser for clunk_small.txt's '0x4 gate' 0x12 time). Usage: python3 -I small_o4_gate.py rows.txt gates.txt"""
import sys, math, collections as C, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'probe'))
import ship_trace_rows as T
CAP = {'M1', 'M2', 'TL', 'M7', 'M0'}
gates = {int(p[0], 16): ([float(x) for x in p[1].split(',')], float(p[3])) for p in (l.split() for l in open(sys.argv[2])) if p[1] != 'insufficient'}
t = C.Counter(); n12 = set(); seen = set(); cls = {}; prev = {}
for l in open(sys.argv[1], errors='replace'):
    if not l.startswith('ship_trace frame='): continue
    r = T.parse_row(l)
    if not r or r['class'] in CAP: continue
    o = r['obj']; p = [x / 500 for x in r['pos']]
    near = any(math.dist(p, G) <= 6 * rg for G, rg in gates.values())
    if near and r['order'] == 4 and r['tgt'] in gates:
        seen.add(o); cls[o] = r['class']
        if o in prev: t[o] += (r['t_ms'] - prev[o]) / 1e3
        prev[o] = r['t_ms']
    else:
        prev.pop(o, None)
    if near and r['order'] == 0x12 and o in seen: n12.add(o)
print('small ships on order 4 to a gate within 6 r:', len(seen), 'by class', dict(C.Counter(cls.values())), 'game s on it %.0f' % sum(t.values()),
      '| of them later on 0x12 within 6 r:', len(n12))
