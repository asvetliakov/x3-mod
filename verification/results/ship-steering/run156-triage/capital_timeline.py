"""Run156 capital timeline from ship_trace rows. Usage: python3 -I capital_timeline.py <ship_trace rows or x3m.log>
Per capital (class M1/M2/TL/M7/M0 with a class-18 target): one line per order segment with game seconds, speed range,
distance to the gate centre (= the order-5 given, the gate origin for capitals) and to the grid point (order-4 given),
axial depth in front of the gate, heading error to the gate axis (F = direction of the order-8 displacement),
emg frames, detour rows (obst != 0). Units: 500 units = 1 m."""
import math, os, sys
from collections import Counter
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'probe'))
import ship_trace_rows as T
CAP = {'M1', 'M2', 'TL', 'M7', 'M0'}
def sub(a, b): return [x - y for x, y in zip(a, b)]
def dot(a, b): return sum(x * y for x, y in zip(a, b))
def nrm(a): return math.sqrt(dot(a, a))
def ang(u, v): return math.degrees(math.acos(max(-1, min(1, dot(u, v) / nrm(u) / nrm(v)))))
rows = {}
with open(sys.argv[1], errors='replace') as f:
    for l in f:
        if not l.startswith('ship_trace frame='): continue
        r = T.parse_row(l)
        if r and r['class'] in CAP: rows.setdefault(r['obj'], []).append(r)
for obj, rs in rows.items():
    gates = Counter(r['tgt'] for r in rs if r['tgt_class'] == 18)
    if not gates: continue
    g = gates.most_common(1)[0][0]
    o5 = [r for r in rs if r['order'] == 5 and r['tgt'] == g]; o8 = [r for r in rs if r['order'] == 8]
    G = [x / 500 for x in o5[-1]['given']] if o5 else None
    F = sub(o8[-1]['pos'], o8[0]['pos']); F = [x / nrm(F) for x in F] if o8 and nrm(F) else None
    grids = Counter(r['given'] for r in rs if r['order'] == 4 and r['tgt'] == g)
    P = [x / 500 for x in grids.most_common(1)[0][0]]
    print('== %s class %s gate 0x%08x rows %d | G_m %s F %s | order-4 givens %s' % (hex(obj), rs[0]['class'], g, len(rs),
          [round(x) for x in G] if G else '-', [round(x, 3) for x in F] if F else '-', len(grids)))
    if G and F:
        d = sub(P, G); print('   grid point rel gate: depth %.0f m lateral %.0f m |P-G| %.0f m' % (dot(d, F), nrm([a - dot(d, F) * b for a, b in zip(d, F)]), nrm(d)))
    segs = []; cur = None
    for r in rs:
        k = (r['order'], r['tgt'])
        if cur is None or cur[0] != k: cur = [k, []]; segs.append(cur)
        cur[1].append(r)
    print('   seg order tgt frames t0_s dur_s n v_ms[min..max] start/end | dG_m start/end | depth_m start/end | dP_m start/end | hdg_err_deg start/end | emg det obst(top)')
    for k, s in segs:
        a, b = s[0], s[-1]
        def m(r, fn): return fn(r) if G and F else float('nan')
        dG = lambda r: nrm(sub([x / 500 for x in r['pos']], G)); dep = lambda r: dot(sub([x / 500 for x in r['pos']], G), F)
        dP = lambda r: nrm(sub([x / 500 for x in r['pos']], P)); he = lambda r: ang(r['fwd'], F)
        vs = [r['v'] / 500 for r in s]; ob = Counter(r['obst'] for r in s if r['obst'])
        print('   0x%-3x 0x%08x %d-%d %.1f %.1f %d %.0f..%.0f %.0f/%.0f | %.0f/%.0f | %.0f/%.0f | %.0f/%.0f | %.0f/%.0f | %d %d %s' % (
            k[0], k[1], a['frame'], b['frame'], a['t_ms'] / 1e3, (b['t_ms'] - a['t_ms']) / 1e3, len(s), min(vs), max(vs), vs[0], vs[-1],
            m(a, dG), m(b, dG), m(a, dep), m(b, dep), dP(a), dP(b), m(a, he), m(b, he), sum(r['emg'] or 0 for r in s), sum(ob.values()),
            ['0x%08x:%d' % kv for kv in ob.most_common(2)]))
    if G and F:
        t3 = next((r for r in rs if dG(r) <= 3 * 1105), None)
        print('   first row within 3 r_gate (3315 m) of G: frame %s t %.1f s; warp end t %.1f s; total %.1f s' % (
            t3['frame'], t3['t_ms'] / 1e3, rs[-1]['t_ms'] / 1e3, (rs[-1]['t_ms'] - t3['t_ms']) / 1e3) if t3 else '   never within 3 r_gate')
        t4 = [r for r in rs if r['order'] == 4 and r['tgt'] == g]
        print('   order-4 total %.1f s in %d segs; order-0x12 total %.1f s in %d segs; first o4 t %.1f s' % (
            sum((s[-1]['t_ms'] - s[0]['t_ms']) / 1e3 for k, s in segs if k[0] == 4), sum(k[0] == 4 for k, s in segs),
            sum((s[-1]['t_ms'] - s[0]['t_ms']) / 1e3 for k, s in segs if k[0] == 0x12), sum(k[0] == 0x12 for k, s in segs), t4[0]['t_ms'] / 1e3))
        for k, s in segs:
            if k[0] == 0x1f:
                h = [he(r) for r in s]; st = [x for x in s if abs(x['v']) > 0]
                print('   0x1f at frame %d: hdg_err start %.1f end %.1f deg, rows with v!=0 %d, max rate |y,p,r| %s, dP %.0f m, dG %.0f m' % (
                    s[0]['frame'], h[0], h[-1], len(st), [max(abs(r['rate'][i]) for r in s) for i in range(3)], dP(s[0]), dG(s[0])))
            if k[0] == 0x12:
                print('   0x12 at frame %d: given rel G (depth, |d|) %.0f, %.0f m; given==point %s; v %.0f..%.0f m/s; dG %.0f->%.0f' % (
                    s[0]['frame'], dot(sub([x / 500 for x in s[0]['given']], G), F), nrm(sub([x / 500 for x in s[0]['given']], G)),
                    s[0]['given'] == s[0]['point'], min(r['v'] for r in s) / 500, max(r['v'] for r in s) / 500, dG(s[0]), dG(s[-1])))
