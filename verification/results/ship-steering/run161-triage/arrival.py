"""Run161 Q2: every ship_gate_launch row (D.4 placement at 0x00444606): class, kind, offset applied (capital: +0.6 r_gate U, the
off_u_m field; small side spawn: off_r/off_u, the delta from the vanilla point), then the ship's first 60 game s from every row with
its order (trace, E rows A/B, warn rows): order runs, 0x12 episodes with the warn's other (class/code, gate = its arrival gate or
another), E rows of (ship, arrival gate) by verdict, and the nearest departing capital (a capital on order 4/5/0x1f/0x0 committed at
the same gate) within the 60 s from E rows (A or B). Usage: python3 -I arrival.py rows.txt"""
import sys, re, bisect, collections as C
KV = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
CAP = {'M1', 'M2', 'TL', 'M7', 'M0'}
caps = {l.split()[0]: l.split()[1] for l in open('caps.txt')}
L = []; tm = {}; S = C.defaultdict(dict); W = C.defaultdict(list); Eg = C.defaultdict(C.Counter); Ecap = C.defaultdict(list); role = C.defaultdict(list)
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        m = re.match(r'ship_trace frame=(\d+) t_ms=(\d+) obj=(\S+) class=\S+ order=(\S+)', l); f = int(m[1]); tm.setdefault(f, int(m[2]))
        S[m[3]][f] = m[4]
    elif l.startswith('ship_gate_launch '):
        L.append(KV(l))
    elif l.startswith('ship_avoid_e_verdict '):
        d = KV(l); f = int(d['frame'])
        S[d['A']].setdefault(f, d['A_order']); S[d['B']].setdefault(f, d['B_order'])
        if d['B_class'] == '18': Eg[(d['A'], d['B'])][(f, d['verdict'])] += 1
        if d['A'] in caps and d['B'] in caps: Ecap[d['A']].append((f, d['B'], float(d['d_m']), d['A_order'], d['B_order'])); Ecap[d['B']].append((f, d['A'], float(d['d_m']), d['B_order'], d['A_order']))
    elif l.startswith('ship_avoid_engine_warn '):
        d = KV(l); S[d['obj']].setdefault(int(d['frame']), d['order']); W[d['obj']].append((int(d['frame']), d['other'], d['class'], d.get('code', '-'), d['d_m']))
    elif l.startswith('ship_gate_ctl '):
        d = KV(l); role[d['ship']].append((int(d['frame']), d['gate'], d['to']))
fs = sorted(tm)
def g(f): i = min(bisect.bisect_left(fs, f), len(fs) - 1); return tm[fs[i]] / 1e3
tot = C.Counter()
for d in L:
    o = d['ship']; f0 = int(d['frame']); t0 = int(d['t_ms']) / 1e3; gate = d['gate']; cap = d['capital'] == '1'
    seq = sorted((f, od) for f, od in S[o].items() if f >= f0 and int(tm.get(f, 0)) / 1e3 - t0 <= 60 if f in tm)
    seq = [(f, od) for f, od in sorted(S[o].items()) if f >= f0 and g(f) - t0 <= 60]
    segs = []
    for f, od in seq:
        if segs and segs[-1][0] == od: segs[-1][2] = f
        else: segs.append([od, f, f])
    e12 = []
    for i, (od, a, b) in enumerate(segs):
        if od != '0x12': continue
        end = segs[i + 1][1] if i + 1 < len(segs) else b
        w = [x for x in W[o] if a - 15 <= x[0] <= a + 5]
        oth = ('arrival gate' if w[0][1] == gate else ('gate ' + w[0][1] if w[0][2] == '18' else '%s/%s' % (w[0][2], w[0][3]))) if w else 'no warn row'
        e12.append('%.1fs@+%.1f vs %s' % (g(end) - g(a), g(a) - t0, oth)); tot[('cap' if cap else 'small', oth.split()[0] if w else oth)] += g(end) - g(a)
    eg = C.Counter(v for (f, v), n in Eg[(o, gate)].items() if g(f) - t0 <= 60 for _ in range(n))
    near = [x for x in Ecap.get(o, []) if f0 <= x[0] and g(x[0]) - t0 <= 60]
    dep = [x for x in near if any(r[1] == gate and r[2] == 'committed' and r[0] <= x[0] for r in role[x[1]])]
    nm = min(near, key=lambda x: x[2]) if near else None
    print('%s %s %s gate %s %s off_r %s off_u %s r_gate %s w %s | orders 60s: %s | 0x12: %s | E(ship,arr gate): %s | nearest capital: %s | departing at same gate met: %s' % (
        d['frame'], o, d['class'], gate[-4:], d['kind'], d['off_r_m'], d['off_u_m'], d['r_gate_m'], d['w_m'],
        ' '.join('%s@%.1f' % (od, g(a) - t0) for od, a, b in segs) or 'no rows', '; '.join(e12) or '-', dict(eg) or '-',
        '%s %s d %.0f @+%.1f orders %s/%s' % (nm[1], caps.get(nm[1]), nm[2], g(nm[0]) - t0, nm[3], nm[4]) if nm else '-', len(dep)))
print('0x12 game s in the first 60 s by (group, other):', {k: round(v, 1) for k, v in tot.items()})
