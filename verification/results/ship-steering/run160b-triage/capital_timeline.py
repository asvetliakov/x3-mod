"""Run158/Run159 capital timeline (copy of the Run157 script, unchanged for Run159) from ship_trace rows (extends the Run156 script).
Usage: python3 -I capital_timeline.py <ship_trace rows or x3m.log> [--legs]
Per capital (class M1/M2/TL/M7/M0 with a class-18 target). Gate frame: G = the capitals' order-5 given (the gate origin);
F (gate axis, row 2) = direction of the order-8 displacement when traced, else the normal of the plane through the small
ships' order-5 pass points B (B - G = r(0.55 sin t R - 0.55 cos t U - 0.01 F)), signed so (G - A_small).F > 0;
r_gate = |A_small - G| / sqrt(1.64) (A_small = G + r(-0.8 U - F)). Grid point P = the capital's order-4 given; its formula
check: depth (P-G).F = -2(r_gate + r_ship), lateral offsets in multiples of 666 m. The capital lead-in window
(gate_lead_in_capital): s = (x - X).F with X = P - 0.75 r F, inside when -6 r <= s <= 0.75 r + 1 m.
Per order segment: game s, speed, dG, depth, dP, heading error to F, s/r, lateral e, rows with point != given
(lead-in active), des vs des_engine (site-B write), emg, obst. Units: 500 units = 1 m.
--legs: per order-4 segment the closest approach to P and the speed / heading / aim there."""
import math, os, sys
from collections import Counter
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'probe'))
import ship_trace_rows as T
CAP = {'M1', 'M2', 'TL', 'M7', 'M0'}
def sub(a, b): return [x - y for x, y in zip(a, b)]
def dot(a, b): return sum(x * y for x, y in zip(a, b))
def nrm(a): return math.sqrt(dot(a, a))
def unit(a): n = nrm(a); return [x / n for x in a]
def cross(a, b): return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
def ang(u, v): return math.degrees(math.acos(max(-1, min(1, dot(u, v) / nrm(u) / nrm(v)))))
M = lambda p: [x / 500 for x in p]
rows = {}; small4 = {}; small5 = {}
with open(sys.argv[1], errors='replace') as f:
    for l in f:
        if not l.startswith('ship_trace frame='): continue
        r = T.parse_row(l)
        if not r: continue
        if r['class'] in CAP: rows.setdefault(r['obj'], []).append(r)
        elif r['tgt_class'] == 18 and r['order'] == 4: small4.setdefault(r['tgt'], Counter())[r['given']] += 1
        elif r['tgt_class'] == 18 and r['order'] == 5: small5.setdefault(r['tgt'], set()).add(r['given'])
legs = '--legs' in sys.argv
for obj, rs in rows.items():
    gates = Counter(r['tgt'] for r in rs if r['tgt_class'] == 18)
    if not gates: continue
    g = gates.most_common(1)[0][0]
    o5 = [r for r in rs if r['order'] == 5 and r['tgt'] == g]; o8 = [r for r in rs if r['order'] == 8]
    G = M(o5[-1]['given']) if o5 else None
    F = None; src = '-'; rg = float('nan')
    if o8 and nrm(sub(o8[-1]['pos'], o8[0]['pos'])): F = unit(sub(o8[-1]['pos'], o8[0]['pos'])); src = 'o8'
    if G and g in small4:
        As = M(small4[g].most_common(1)[0][0]); rg = nrm(sub(As, G)) / math.sqrt(1.64)
        Bs = [M(b) for b in small5.get(g, ())]
        if F is None and len(Bs) >= 3:
            best = None
            for i in range(len(Bs)):
                for j in range(i + 1, len(Bs)):
                    for k in range(j + 1, len(Bs)):
                        c = cross(sub(Bs[j], Bs[i]), sub(Bs[k], Bs[i]))
                        if best is None or nrm(c) > nrm(best): best = c
            F = unit(best); F = F if dot(sub(G, As), F) > 0 else [-x for x in F]; src = 'B-plane(%d)' % len(Bs)
    grids = Counter(r['given'] for r in rs if r['order'] == 4 and r['tgt'] == g)
    P = M(grids.most_common(1)[0][0])
    print('== %s class %s gate 0x%08x rows %d | G_m %s F %s (%s) r_gate %.0f m | order-4 givens %d' % (hex(obj), rs[0]['class'], g, len(rs),
          [round(x) for x in G] if G else '-', [round(x, 3) for x in F] if F else '-', src, rg, len(grids)))
    if not (G and F): continue
    d = sub(P, G); dep = dot(d, F); lat = sub(d, [dep * x for x in F])
    up = None
    print('   grid point P %s rel G: depth %.0f m (=> r_ship %.0f m from -2(r_gate+r_ship)) lateral %.0f m (%.2f x 666 m) |P-G| %.0f m' % (
          [round(x) for x in P], dep, -dep / 2 - rg, nrm(lat), nrm(lat) / 666, nrm(d)))
    X = [p - 0.75 * rg * x for p, x in zip(P, F)]
    sR = lambda r: dot(sub(M(r['pos']), X), F) / rg
    eL = lambda r: (lambda dx: math.sqrt(max(0, dot(dx, dx) - dot(dx, F) ** 2)))(sub(M(r['pos']), X))
    dG = lambda r: nrm(sub(M(r['pos']), G)); dp = lambda r: dot(sub(M(r['pos']), G), F)
    dP = lambda r: nrm(sub(M(r['pos']), P)); he = lambda r: ang(r['fwd'], F)
    aim = lambda r: ang(r['fwd'], sub(r['point'], r['pos'])) if nrm(sub(r['point'], r['pos'])) else 0.0
    segs = []; cur = None
    for r in rs:
        k = (r['order'], r['tgt'])
        if cur is None or cur[0] != k: cur = [k, []]; segs.append(cur)
        cur[1].append(r)
    print('   order tgt frames t0_s dur_s n | v_ms min..max start/end | dG_m s/e | depth_m s/e | dP_m s/e | hdg_err s/e | s_r s/e | e_m s/e | lead rows(in window) | des!=des_engine rows | emg | obst(top)')
    for k, s in segs:
        a, b = s[0], s[-1]; vs = [r['v'] / 500 for r in s]; ob = Counter(r['obst'] for r in s if r['obst'])
        lead = sum(r['point'] != r['given'] for r in s); win = sum(-6 <= sR(r) <= 0.75 for r in s)
        print('   0x%-3x 0x%08x %d-%d %.1f %.1f %d | %.0f..%.0f %.0f/%.0f | %.0f/%.0f | %.0f/%.0f | %.0f/%.0f | %.0f/%.0f | %.2f/%.2f | %.0f/%.0f | %d(%d) | %d | %d | %s' % (
            k[0], k[1], a['frame'], b['frame'], a['t_ms'] / 1e3, (b['t_ms'] - a['t_ms']) / 1e3, len(s), min(vs), max(vs), vs[0], vs[-1],
            dG(a), dG(b), dp(a), dp(b), dP(a), dP(b), he(a), he(b), sR(a), sR(b), eL(a), eL(b), lead, win,
            sum(r['des'] != r['des_engine'] for r in s), sum(r['emg'] or 0 for r in s), ['0x%08x:%d' % kv for kv in ob.most_common(3)]))
        if legs and k[0] == 4:
            m = min(s, key=dP)
            print('      closest to P: frame %d dP %.0f m (arrival 0.75 r = %.0f m) v %.0f m/s des %.0f des_engine %.0f hdg_err %.0f aim_err %.0f s_r %.2f e %.0f m lead %s' % (
                m['frame'], dP(m), 0.75 * rg, m['v'] / 500, m['des'] / 500, m['des_engine'] / 500, he(m), aim(m), sR(m), eL(m), m['point'] != m['given']))
    t3 = next((r for r in rs if dG(r) <= 3 * rg), None)
    print('   first row within 3 r_gate of G: %s; last row t %.1f s' % ('frame %d t %.1f s (%.1f s before the last row)' % (
          t3['frame'], t3['t_ms'] / 1e3, (rs[-1]['t_ms'] - t3['t_ms']) / 1e3) if t3 else 'never', rs[-1]['t_ms'] / 1e3))
    tot = lambda o: sum((s[-1]['t_ms'] - s[0]['t_ms']) / 1e3 for k, s in segs if k[0] == o)
    print('   order-4 total %.1f s in %d segs; order-0x12 total %.1f s in %d segs; 0x1f %.1f s; 5 %.1f s; rows in the 6 r window %d of %d order-4 rows, lead rows %d' % (
          tot(4), sum(k[0] == 4 for k, s in segs), tot(0x12), sum(k[0] == 0x12 for k, s in segs), tot(0x1f), tot(5),
          sum(-6 <= sR(r) <= 0.75 for r in rs if r['order'] == 4), sum(r['order'] == 4 for r in rs), sum(r['point'] != r['given'] for r in rs if r['order'] == 4)))
    for k, s in segs:
        if k[0] == 0x1f:
            h = [he(r) for r in s]
            print('   0x1f at frame %d: hdg_err start %.1f end %.1f deg, v start %.0f m/s, dP %.0f m, dG %.0f m, s_r %.2f' % (
                s[0]['frame'], h[0], h[-1], s[0]['v'] / 500, dP(s[0]), dG(s[0]), sR(s[0])))
