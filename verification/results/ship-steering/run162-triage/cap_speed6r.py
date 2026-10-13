"""Run161 Q3: per traced capital, the game time inside 6 r_gate (order 4 before the plane, and 0x1f/0x0), split by who sets the speed:
eng_park (des_engine < 20 m/s: the script's grid parking / through-slot wait speed) with ours raising it (des > des_engine) or not;
ours_lower (des < des_engine - 0.5); free (des == des_engine >= 20). For each: v quartiles (m/s) and the vmax (m/s). Join modes from
ship_gate_capital_join (v_cmd quartiles per mode). Usage: python3 -I cap_speed6r.py rows.txt gates.txt [--r 6]"""
import sys, re, math, bisect, statistics as St, collections as C, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'probe'))
import ship_trace_rows as T
CAP = {'M1', 'M2', 'TL', 'M7', 'M0'}
KR = float(sys.argv[sys.argv.index('--r') + 1]) if '--r' in sys.argv else 6.0
gates = [(p[0], [float(x) for x in p[1].split(',')], [float(x) for x in p[2].split(',')], float(p[3])) for p in (l.split() for l in open(sys.argv[2]))]
rows = C.defaultdict(list); jm = C.defaultdict(list)
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        r = T.parse_row(l)
        if r and r['class'] in CAP: rows['0x%08x %s' % (r['obj'], r['class'])].append(r)
    elif l.startswith('ship_gate_capital_join '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); jm[d['obj']].append((int(d['frame']), d['mode'], float(d['v_cmd'])))
q = lambda v: '%.0f/%.0f/%.0f' % (min(v), St.median(v), max(v)) if v else '-'
for o, rs in rows.items():
    t = C.Counter(); V = C.defaultdict(list); vmax = 0
    for a, b in zip(rs, rs[1:]):
        p = [x / 500 for x in a['pos']]
        g = min(gates, key=lambda g: math.dist(p, g[1]))
        s = sum((x - y) * f for x, y, f in zip(p, g[1], g[2]))
        if math.dist(p, g[1]) > KR * g[3] or s > 0 or a['order'] not in (0x4, 0x1f, 0x0): continue
        dt = (b['t_ms'] - a['t_ms']) / 1e3; de = a['des_engine'] / 500; ds = a['des'] / 500; v = a['v'] / 500; vmax = max(vmax, a['vmax'] / 500)
        k = 'rotate' if a['order'] == 0x1f else ('ours_lower' if ds < de - 0.5 else ('eng_park_raised' if de < 20 and ds > de + 0.5 else ('eng_park' if de < 20 else 'free')))
        t[k] += dt; V[k].append(v)
    if not t: continue
    o0 = o.split()[0]; modes = C.defaultdict(list)
    for f, m, vc in jm[o0]: modes[m].append(vc)
    print(o, 'band %.0f r' % KR, 'vmax %.0f' % vmax, ' '.join('%s %.1f s v %s' % (k, t[k], q(V[k])) for k in sorted(t)), '| join v_cmd by mode', {m: q(v) for m, v in modes.items()})
