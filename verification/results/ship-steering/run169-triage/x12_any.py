# Run169 Q2: every 0x12 (engine avoid) episode on a traced capital: the ship's member role/stage at its start, the 0x12 target (trace tgt),
# the target's class and its own member role/stage/order, the distance between them, and whether both are gate-controller members.
# Usage: python3 -I x12_any.py rows.txt
import sys, re, math, collections as C
ids = {}; mem = C.defaultdict(list); last = {}; ep = []; cls = {}; pos = {}
cur = {}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids.setdefault(d['ship'], d['id'])
    elif l.startswith('ship_gate_ctl_member '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); last[d['ship']] = (int(d['t_ms']), d['role'], d['stage'], d['order'], d['gate'][-4:], d['v_ms'])
    elif l.startswith('ship_trace frame='):
        m = re.match(r'ship_trace frame=(\d+) t_ms=(\d+) obj=(\S+) class=(\S+) order=(\S+) tgt=(\S+) tgt_class=(\S+) pos=(\S+)', l)
        if not m: continue
        f, t, o, c, od, tg, tc, p = int(m[1]), int(m[2]), m[3], m[4], m[5], m[6], m[7], [float(x) / 500 for x in m[8].split(',')]
        cls[o] = c; pos[o] = (t, p)
        if od == '0x12':
            if o not in cur or t - cur[o]['t1'] > 3000:
                if o in cur: ep.append(cur[o])
                op = pos.get(tg); dd = math.dist(p, op[1]) if op and t - op[0] < 2000 else float('nan')
                cur[o] = dict(o=o, c=c, f0=f, t0=t, t1=t, tg=tg, tc=tc, me=last.get(o), oth=last.get(tg), d=dd, tgcls=cls.get(tg, '?'))
            cur[o]['t1'] = t
ep += cur.values()
print('ship class | game t0 dur s | own member (age s role stage order gate v) | 0x12 tgt id class(tgt_class) | tgt member | d_m at start | cap-vs-cap members')
for e in sorted(ep, key=lambda e: e['t0']):
    me, ot = e['me'], e['oth']
    fm = lambda r: '%.0f %s %s %s %s %s' % ((e['t0'] - r[0]) / 1000, *r[1:]) if r else '-'
    both = bool(me and ot and me[1] not in ('evicted',) and ot[1] != 'none') and e['tgcls'] in ('M1', 'M2', 'M7', 'TL', 'M0')
    print(ids.get(e['o'], e['o']), e['c'], '| %.1f %.1f' % (e['t0'] / 1000, (e['t1'] - e['t0']) / 1000), '|', fm(me), '|', ids.get(e['tg'], e['tg']), e['tgcls'], '(%s)' % e['tc'], '|', fm(ot), '| %.0f' % e['d'], '|', both)
