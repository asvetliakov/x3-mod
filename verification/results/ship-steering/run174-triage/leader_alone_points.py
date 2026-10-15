# Run174 follow-up: the group-2 leader's leaf point (point=, after our lane/avoidance) and the order field (given=) in the gate frame
# (x, y lateral; z = s along F = (0,0,-1): negative in front) every ~8 game s from its arrival to the first escort's arrival + 20 s.
# Usage: python3 -I leader_alone_points.py ROWS
import sys, re
G = (-7498, 10774, -57569); last = -1e9
for l in open(sys.argv[1], errors='replace'):
    if not l.startswith('ship_trace frame=') or 'obj=0x39e43950' not in l: continue
    d = dict(re.findall(r'(\w+)=(\S+)', l)); t = int(d['t_ms'])
    if t > 23950000: break
    if t - last < 8000: continue
    last = t
    gf = lambda v: '%.0f,%.0f,%.0f' % tuple((int(x) / 500 - G[i]) * (-1 if i == 2 else 1) * (1 if i < 2 else 1) for i, x in enumerate(v.split(',')))
    # gate frame: R=(1,0,0), U=(0,1,0), F=(0,0,-1): s = -(z - Gz)
    def gfr(v):
        p = [int(x) / 500 for x in v.split(',')]; return '%.0f,%.0f,s=%.0f' % (p[0] - G[0], p[1] - G[1], -(p[2] - G[2]))
    print('g=%.1f ord=%s pos %s | leaf point %s | field %s' % (t / 1000, d['order'], gfr(d['pos']), gfr(d['point']), gfr(d['given'])))
