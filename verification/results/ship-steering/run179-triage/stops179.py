# Run179 Q1/Q3: per member of a departure (ship, gate, window), the kill criterion's stop runs (v < 15 m/s >= 3 s) and turn-away runs
# (p_m F < -2500 and nose_f < 0 >= 3 s), each with |p|, lane_s, lat, the laws (law:count), the writes w (w:count), v_law min, the
# yield partner fields (rank, gap), and the lane stop writes (w=stop rows) as runs of consecutive frames.
# Usage: python3 -I stops179.py ROWS GATE T_LO_S T_HI_S SHIP[,SHIP...]
import sys, re, math, collections as C
rows, gate, lo, hi, ships = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4]), sys.argv[5].split(',')
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
mem = {}; R = C.defaultdict(list)
for l in open(rows, errors='replace'):
    if not l.startswith('ship_gate_ctl_') or 'gate=' + gate not in l: continue
    if l.startswith('ship_gate_ctl_member '):
        d = kv(l)
        if d['ship'] in ships: mem[d['ship']] = d
        continue
    if not l.startswith('ship_gate_ctl_lane '): continue
    d = kv(l); m = mem.pop(d['ship'], None)
    if not m or m['frame'] != d['frame']: continue
    t = int(d['t_ms']) / 1000
    if not lo <= t <= hi or m['role'] == 'arrival': continue
    p = [float(x) for x in m['p_m'].split(',')]
    R[d['ship']].append((t, p, float(m['nose_f']), float(m['v_ms']), m['role'], m['stage'], m['order'], d['law'], d['w'], d['v_law'], d['rank'], d['gap_m'], d['lat_m'], d.get('own'), m.get('funnel'), d.get('loop')))
def runs(xs, f, min_s=3.0):
    out = []; st = None
    for i, x in enumerate(xs):
        if f(x):
            if st is None: st = i
        elif st is not None:
            out.append((st, i - 1)); st = None
    if st is not None: out.append((st, len(xs) - 1))
    return [(a, b) for a, b in out if xs[b][0] - xs[a][0] >= min_s]
def desc(xs, a, b):
    seg = xs[a:b + 1]
    laws = C.Counter(x[7] for x in seg); ws = C.Counter(x[8] for x in seg); ords = C.Counter(x[6] for x in seg); stg = C.Counter('%s/%s' % (x[4], x[5]) for x in seg)
    vl = [float(x[9]) for x in seg if x[9] != 'na']
    return ('%.1f+%.1f |p| %.0f->%.0f s %.0f->%.0f lat %s->%s nose %.2f->%.2f vmin %.1f | %s | laws %s | w %s | ord %s | v_law min %s | rank %s gap %s->%s own %s fun %s loop %s'
            % (seg[0][0], seg[-1][0] - seg[0][0], math.hypot(*seg[0][1]), math.hypot(*seg[-1][1]), seg[0][1][2], seg[-1][1][2], seg[0][12], seg[-1][12],
               seg[0][2], seg[-1][2], min(x[3] for x in seg), dict(stg), dict(laws), dict(ws), dict(ords), min(vl) if vl else 'na', seg[0][10], seg[0][11], seg[-1][11],
               seg[0][13], seg[0][14], seg[0][15]))
for s in ships:
    xs = R[s]
    print('%s rows %d %s' % (s, len(xs), '%.1f..%.1f' % (xs[0][0], xs[-1][0]) if xs else ''))
    for a, b in runs(xs, lambda x: x[3] < 15.0): print('  stop  ' + desc(xs, a, b))
    for a, b in runs(xs, lambda x: x[1][2] < -2500.0 and x[2] < 0.0): print('  turn  ' + desc(xs, a, b))
    for a, b in runs(xs, lambda x: x[8] == 'stop', 0.0): print('  wstop ' + desc(xs, a, b))
