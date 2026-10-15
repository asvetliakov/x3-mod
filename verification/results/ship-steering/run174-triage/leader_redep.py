# Run174 follow-up: runs of the group-2 leader's leaf point at the lane's re-departure turn point (gate frame s <= -6000 m, the point
# 1 km below Lambda: y < -1500) on order 4, i.e. the lane's "out first" leg (ship_gate_ctl_core.h redep), with game times and positions.
# Usage: python3 -I leader_redep.py ROWS
import sys, re
G = (-7498, 10774, -57569); run = None; out = []
for l in open(sys.argv[1], errors='replace'):
    if not l.startswith('ship_trace frame=') or 'obj=0x39e43950' not in l: continue
    d = dict(re.findall(r'(\w+)=(\S+)', l)); t = int(d['t_ms']) / 1000
    P = [int(x) / 500 for x in d['point'].split(',')]; X = [int(x) / 500 for x in d['pos'].split(',')]
    sP = -(P[2] - G[2]); yP = P[1] - G[1]; sX = -(X[2] - G[2])
    hit = d['order'] == '0x4' and sP <= -6000 and yP < -1500
    if hit and not run: run = [t, t, sX, sX, '%.0f,%.0f' % (yP, sP)]
    elif hit: run[1] = t; run[3] = sX
    elif run: out.append(run); run = None
if run: out.append(run)
for a, b, s0, s1, p in out: print('redep leg %.1f-%.1f (%.1f s) ship s %.0f -> %.0f, point y,s %s' % (a, b, b - a, s0, s1, p))
print('legs', len(out), 'total %.1f s game' % sum(b - a for a, b, *_ in out))
