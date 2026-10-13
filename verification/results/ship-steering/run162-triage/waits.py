"""Run160b Q3: traced capitals' waits: runs of v < 20 m/s lasting > 5 game s on order 0x4/0x1f/0x0 within 6 r_gate of a gate
centre (gates.txt). Per run: order mix, des vs des_engine (our write lowers des when des < des_engine), emg, obst, join mode,
lane rows of A in the run (verdict, v_cap >= 0 = the (b) cap). Also order-0x1d (SA_Idle) rows of any capital.
Usage: python3 -I waits.py rows.txt gates.txt"""
import sys, re, math, collections as C, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'probe'))
import ship_trace_rows as T
CAP = {'M1', 'M2', 'TL', 'M7', 'M0'}
GT = {}
for l in open(sys.argv[2]):
    p = l.split()
    if len(p) > 3 and p[1] != 'insufficient': GT[p[0]] = ([float(x) for x in p[1].split(',')], float(p[3]))
rows = C.defaultdict(list); lanes = C.defaultdict(list); joins = C.defaultdict(list); o1d = C.Counter()
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        r = T.parse_row(l)
        if r and r['class'] in CAP: rows[r['obj']].append(r)
    elif l.startswith('ship_capital_lane '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); lanes[int(d['A'], 16)].append(d)
    elif l.startswith('ship_gate_capital_join '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); joins[int(d['obj'], 16)].append((int(d['frame']), d['mode']))
    elif l.startswith('ship_avoid_e_verdict '):
        m = re.search(r' A=(\S+) A_order=0x1d ', l)
        if m: o1d[m[1]] += 1
def near(p):
    return min(((math.dist(p, G), k) for k, (G, rg) in GT.items() if math.dist(p, G) <= 6 * rg), default=None)
n = 0
for o, rs in rows.items():
    run = []
    def flush():
        global n
        if run and (run[-1]['t_ms'] - run[0]['t_ms']) / 1e3 > 5:
            n += 1; a, b = run[0], run[-1]
            L = [d for d in lanes.get(o, []) if a['frame'] <= int(d['frame']) <= b['frame']]
            js = [m for f, m in joins.get(o, []) if f <= b['frame']]
            print('0x%08x %s frames %d-%d game %.1f dur %.1f | orders %s | gate %s dist %.0f m | v %.1f..%.1f | des<des_engine rows %d | emg %d | obst %s | join mode %s | lane %s v_cap>=0 %d' % (
                o, a['class'], a['frame'], b['frame'], a['t_ms'] / 1e3, (b['t_ms'] - a['t_ms']) / 1e3, dict(C.Counter(hex(x['order']) for x in run)),
                near([v / 500 for v in a['pos']])[1], near([v / 500 for v in a['pos']])[0], min(x['v'] for x in run) / 500, max(x['v'] for x in run) / 500,
                sum(x['des'] < x['des_engine'] for x in run), sum(bool(x['emg']) for x in run), dict(C.Counter('0x%08x' % x['obst'] for x in run if x['obst'])),
                js[-1] if js else '-', dict(C.Counter(d['verdict'] for d in L)), sum(float(d['v_cap']) >= 0 for d in L)))
        run.clear()
    for r in rs:
        p = [v / 500 for v in r['pos']]
        if r['v'] / 500 < 20 and r['order'] in (0x4, 0x1f, 0x0) and near(p): run.append(r)
        else: flush()
    flush()
print('waits:', n, '| capital E rows with A_order 0x1d (SA_Idle):', dict(o1d) or 0)
