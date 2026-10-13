"""Run159 triage, question 6: each engine warn of a traced small ship against a gate (ship_avoid_engine_warn class=18): the
subject's last order-4 row before it (position, its given A_small, v), s = (x - G).F and lateral distance from the axis, and the
small-ship gate row's path guard dist(G, segment[x, A_small]) against r_gate + r_B + 0.1 r_gate (design 16.4; r_B = the
subject's own radius from the warn_pre row is not logged, so r_B is printed as the guard's slack without it).
G and F from capital_timeline's frame (order-5 given of the capitals; F from the small ships' order-5 points); here passed in.
Run160b: the gate frame per warn comes from gates.txt (gates.py, every gate in the log) by the warn's other id, not argv;
the subject's last order-4 row must target that gate (else 'tgt differs' is printed). Small subjects only (capital subjects
are in cap_orders.txt). pass = guard >= 0 (r_B not logged).
Usage: python3 -I small_gate_warn.py <extracted rows> <gates.txt>"""
import math, re, sys
GT = {}
for l in open(sys.argv[2]):
    p = l.split()
    if len(p) > 3 and p[1] != 'insufficient': GT[p[0]] = ([float(x) for x in p[1].split(',')], [float(x) for x in p[2].split(',')], float(p[3]))
CAPC = {'M1', 'M2', 'TL', 'M7', 'M0'}
W = []; last = {}
def segdist(p, a, b):
    ab = [y - x for x, y in zip(a, b)]; ap = [y - x for x, y in zip(a, p)]; L = sum(x * x for x in ab)
    t = max(0, min(1, sum(x * y for x, y in zip(ab, ap)) / L)) if L else 0
    q = [x + t * y for x, y in zip(a, ab)]; return math.dist(p, q)
hist = {}; rB = {}
with open(sys.argv[1], errors='replace') as f:
    for l in f:
        if l.startswith('ship_trace frame='):
            m = re.match(r'ship_trace frame=(\d+) t_ms=\d+ obj=(\S+) class=(\S+) order=(\S+) tgt=(\S+) tgt_class=(\d+) pos=(\S+) .* v=(\d+) .*given=(\S+) e=', l)
            if m and m.group(4) == '0x4' and m.group(6) == '18':
                hist[m.group(2)] = (int(m.group(1)), m.group(3), [int(x) / 500 for x in m.group(7).split(',')], int(m.group(8)) / 500, [int(x) / 500 for x in m.group(9).split(',')], m.group(5))
        elif l.startswith('ship_avoid_e_verdict '):
            m = re.search(r' B=(\S+) .* r_B=(\S+)', l)
            if m: rB[m.group(1)] = float(m.group(2))
        elif l.startswith('ship_avoid_engine_warn ') and ' class=18 ' in l:
            d = dict(re.findall(r'(\w+)=(\S+)', l)); W.append((d, hist.get(d['obj'])))
print('frame obj class d_m | last order-4 row frame, v | s_m lat_m (x vs G) | A_small s_m lat_m | guard dist(G,[x,A]) - 1.1 r_gate - r_B (r_B from E rows; pass >= 0 means the guard would clear)')
from collections import Counter
res = Counter()
for d, h in W:
    if d['other'] not in GT: print(d['frame'], d['obj'], 'gate', d['other'], 'not in gates.txt'); res['no_gate'] += 1; continue
    G, F, rg = GT[d['other']]
    if not h: print(d['frame'], d['obj'], 'gate', d['other'], 'no order-4 trace row (untraced or capital)'); res['untraced'] += 1; continue
    fr, cls, x, v, A, tgt = h
    if cls in CAPC: continue
    rel = lambda p: (sum((a - b) * c for a, b, c in zip(p, G, F)), math.sqrt(max(0, sum((a - b) ** 2 for a, b in zip(p, G)) - sum((a - b) * c for a, b, c in zip(p, G, F)) ** 2)))
    sx, lx = rel(x); sa, la = rel(A)
    gd = segdist(G, x, A) - 1.1 * rg - rB.get(d['obj'], 0); res[('pass' if gd >= 0 else 'fail') + ('' if tgt == d['other'] else '_tgt_differs')] += 1
    print('%s %s %s %s gate %s | %d %.0f | %.0f %.0f | %.0f %.0f | %.0f  (tgt %s)%s' % (d['frame'], d['obj'], cls, d['d_m'], d['other'], fr, v, sx, lx, sa, la, gd, tgt, '' if tgt == d['other'] else ' TGT_DIFFERS'))
print('summary', dict(res))
