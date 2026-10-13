"""Run161 Q5: the ship_avoid_dock rows (amendment 18): ships and containers, verdicts by (order, zone), parts model validity, carriers
(container class 7 = a ship), the summary dock counters (last row), 0x12 episodes whose warn other is the ship's own dock container
(ship_avoid_engine_warn other == cont) on order 4, and order-0x1d (queue idle) trace time per ship near its container.
Usage: python3 -I dock.py rows.txt"""
import sys, re, math, collections as C, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'probe'))
import ship_trace_rows as T
KV = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
R = []; last = None; warns = []; o1d = C.Counter(); o1d_v = C.defaultdict(list); tr_cls = {}; contcls = {}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_avoid_dock '): R.append(KV(l))
    elif l.startswith('ship_steering_summary '): last = l
    elif l.startswith('ship_avoid_engine_warn '): warns.append(KV(l))
    elif l.startswith('ship_avoid_e_verdict '):
        d = KV(l); contcls[d['B']] = d['B_class']
    elif l.startswith('ship_trace frame='):
        r = T.parse_row(l)
        if r:
            tr_cls['0x%08x' % r['obj']] = r['class']
            if r['order'] == 0x1d: o1d['0x%08x' % r['obj']] += 1; o1d_v['0x%08x' % r['obj']].append(r['v'] / 500)
print('rows', len(R), 'ships', len({d['ship'] for d in R}), 'containers', len({d['cont'] for d in R}), 'pairs', len({(d['ship'], d['cont']) for d in R}))
print('containers (cont, class from rows, E B_class, n_parts, r_c_m, ships):')
for c in sorted({d['cont'] for d in R}):
    rs = [d for d in R if d['cont'] == c]
    print('  ', c, 'row class', C.Counter(d['class'] for d in rs).most_common(2), 'E class', contcls.get(c, '?'), 'n_parts', set(d['n_parts'] for d in rs), 'r_c_m', set(d['r_c_m'] for d in rs), 'ships', len({d['ship'] for d in rs}), 'rows', len(rs))
print('by verdict:', dict(C.Counter(d['e'] for d in R)))
print('by (order, zone, verdict):', C.Counter((d['order'], d['zone'], d['e']) for d in R).most_common(20))
print('parts valid:', dict(C.Counter(d['parts'] for d in R)), 'kept:', dict(C.Counter(d['kept'] for d in R)), 'detour:', dict(C.Counter(d['detour'] for d in R)))
pas = [d for d in R if d['e'].startswith('pass')]
print('pass rows:', len(pas), [(d['frame'], d['ship'], tr_cls.get(d['ship'], '?'), d['cont'], d['order'], d['zone'], d['e'], 'path', d['path_m'], 'ray', d['ray_m'], 'reach', d['reach_m'], 'd_c', d['d_c_m'], 'v', d['v_ms']) for d in pas[:6]])
own = {(d['ship'], d['cont']) for d in R}
w_own = [w for w in warns if (w['obj'], w['other']) in own]
print('engine warns (0x12) whose other is the ship\'s own dock container:', len(w_own), [(w['frame'], w['obj'], w['other'], w['d_m']) for w in w_own[:5]])
d = KV(last)
print('summary:', {k: d[k] for k in d if k.startswith(('dock', 'e_pass_dock', 'e_suppressed_dock'))})
print('order 0x1d (queue idle) traced rows per ship:', {o: (n, '%.0f..%.0f m/s' % (min(o1d_v[o]), max(o1d_v[o]))) for o, n in o1d.most_common(8)})
