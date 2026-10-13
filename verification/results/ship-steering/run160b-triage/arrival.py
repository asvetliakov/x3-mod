"""Run160b Q5: the first 60 game s of each capital that appeared at the arrival gate 0x8e1ef948. From every row with the
capital's order (cap_orders sources), its order sequence; from E rows (A = the capital): distance to the gate (B = the gate)
and to other capitals over time; warn rows; for the traced d0090 the position relative to the gate axis (s along F, lateral).
Usage: python3 -I arrival.py rows.txt"""
import sys, re, bisect, math, collections as C
caps = {l.split()[0]: l.split()[1] for l in open('caps.txt')}
ARR = ['0x8e1d0090', '0x8e1dc258', '0x8e212188', '0x8e223c48', '0xa908da10', '0xa9091e50', '0x8e1c9418']
GATE = '0x8e1ef948'; G = [-7498.0, 10774.0, -57569.0]; F = [0, 0, -1.0]
tm = {}; S = C.defaultdict(dict); Eg = C.defaultdict(list); Ec = C.defaultdict(list); pos = C.defaultdict(list); Es = C.defaultdict(C.Counter)
first = {}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        m = re.match(r'ship_trace frame=(\d+) t_ms=(\d+) obj=(\S+) class=\S+ order=(\S+) tgt=\S+ tgt_class=\d+ pos=(\S+) fwd=(\S+) .* v=(\d+)', l); f = int(m[1])
        tm.setdefault(f, int(m[2]))
        if m[3] in ARR: S[m[3]][f] = m[4]; pos[m[3]].append((f, [int(x) / 500 for x in m[5].split(',')], [float(x) for x in m[6].split(',')], int(m[7]) / 500))
    elif l.startswith('ship_avoid_e_verdict '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); f = int(d['frame'])
        if d['A'] in ARR:
            S[d['A']].setdefault(f, d['A_order'])
            if d['B'] == GATE: Eg[d['A']].append((f, float(d['d_m']), d['A_order'], d['verdict']))
            elif d['B'] in caps: Ec[d['A']].append((f, d['B'], float(d['d_m']), d['A_order'], d['B_order'], d['verdict']))
            else: Es[d['A']][(d['B_class'], d['B_code'], d['verdict'])] += 1 if False else 0
        if d['B'] in ARR: S[d['B']].setdefault(f, d['B_order'])
    elif l.startswith(('ship_avoid_engine_warn ', 'ship_avoid_engine_warn_pre ')):
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        if d['obj'] in ARR: S[d['obj']].setdefault(int(d['frame']), d['order'])
fs = sorted(tm)
def g(f): i = min(bisect.bisect_left(fs, f), len(fs) - 1); return tm[fs[i]] / 1e3
for o in ARR:
    seq = sorted(S[o].items()); f0 = seq[0][0]; t0 = g(f0); W = [x for x in seq if g(x[0]) - t0 <= 60]
    segs = []
    for f, od in W:
        if segs and segs[-1][0] == od: segs[-1][2] = f
        else: segs.append([od, f, f])
    print('== %s %s first row frame %d game %.1f; order runs in 60 s: %s' % (o, caps[o], f0, t0, ' '.join('%s@%.1f' % (od, g(a) - t0) for od, a, b in segs)))
    eg = [x for x in Eg[o] if g(x[0]) - t0 <= 60]
    if eg: print('   d(gate) m over t (s): %s' % ' '.join('%.0f@%.0f' % (x[1], g(x[0]) - t0) for x in eg[::max(1, len(eg) // 10)]))
    ec = [x for x in Ec[o] if g(x[0]) - t0 <= 60]
    pr = {}
    for x in ec:
        if x[1] not in pr or x[2] < pr[x[1]][2]: pr[x[1]] = x
    for b, x in pr.items(): print('   nearest capital in 60 s: %s %s min d %.0f m at +%.1f s orders %s/%s verdict %s' % (b, caps[b], x[2], g(x[0]) - t0, x[3], x[4], x[5]))
    ps = [p for p in pos[o] if g(p[0]) - t0 <= 90]
    for p in ps[::max(1, len(ps) // 6)]:
        d = [a - b for a, b in zip(p[1], G)]; s = sum(a * b for a, b in zip(d, F)); lat = math.sqrt(max(0, sum(a * a for a in d) - s * s))
        print('   traced +%.1f s order %s s %.0f m lateral %.0f m |x-G| %.0f v %.0f fwd.F %.2f' % (g(p[0]) - t0, S[o][p[0]], s, lat, math.dist(p[1], G), p[3], sum(a * b for a, b in zip(p[2], F))))
