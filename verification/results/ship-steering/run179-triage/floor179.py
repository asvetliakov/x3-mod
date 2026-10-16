# Run179 Q1: per member lane (ship, gate), the rows where the law holds it at or below the floor (law merge or stop with v_law <= 10.5 m/s)
# and, of those, the rows more than 3 km ahead of its predecessor (gap_m < -3000: the casts' Fx "no crawl at the floor beyond 3 km of the
# leader"; the predecessor is the leader or the escort ahead, per the merge rows). Usage: python3 -I floor179.py ROWS SHIP:GATE ...
import sys, re
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
want = {tuple(a.split(':')): [] for a in sys.argv[2:]}
for l in open(sys.argv[1], errors='replace'):
    if not l.startswith('ship_gate_ctl_lane '): continue
    d = kv(l); k = (d['ship'], d['gate'])
    if k in want: want[k].append((int(d['t_ms']) / 1e3, float(d['v_law']), d['gap_m'], d['law']))
for k, xs in want.items():
    fl = [x for x in xs if x[3] in ('merge', 'stop') and x[1] <= 10.5]
    far = [x for x in fl if x[2] != 'na' and float(x[2]) < -3000]
    print('%s at %s: floor/stop rows %d%s%s' % (k[0], k[1], len(fl), ' %.1f-%.1f' % (fl[0][0], fl[-1][0]) if fl else '',
          ' | gap < -3 km: %d rows %.1f-%.1f' % (len(far), far[0][0], far[-1][0]) if far else ''))
