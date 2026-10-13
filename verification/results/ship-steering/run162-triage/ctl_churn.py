"""Run161 Q1/Q7: controller membership churn from ship_gate_ctl rows: leaves by (from, why, order field), and for escorts the gap from
a 'why=order' leave to the next entry of the same ship (frames and game s), i.e. how long an escort is off the board.
Usage: python3 -I ctl_churn.py rows.txt"""
import sys, re, bisect, statistics as St, collections as C
R = []; tm = {}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '): R.append(dict(re.findall(r'(\w+)=(\S+)', l)))
c = C.Counter((d['from'], d['to'], d['why'], d['order']) for d in R)
print('transitions (from, to, why, order):', c.most_common(14))
by = C.defaultdict(list)
for d in R: by[d['ship']].append(d)
gaps = []
for s, rs in by.items():
    for a, b in zip(rs, rs[1:]):
        if a['why'] == 'order' and a['to'] == 'none' and b['to'] in ('escort', 'committed'):
            gaps.append(((int(b['t_ms']) - int(a['t_ms'])) / 1e3, int(b['frame']) - int(a['frame'])))
g = sorted(x[0] for x in gaps)
print('escort order-leaves followed by a re-entry:', len(g), 'off-board game s min/med/p90/max %.1f/%.1f/%.1f/%.1f' % (g[0], St.median(g), g[int(0.9 * len(g))], g[-1]),
      'sum %.1f s' % sum(g))
ent = [d for d in R if d['to'] == 'escort']
per = C.Counter(d['ship'] for d in ent)
print('escort entries per ship:', dict(per))
