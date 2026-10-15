# Run177 Q1: was section 13's arrival mode exercised? For every capital gate launch (an arrival at gate X), the first later board entry
# (ship_gate_ctl to=committed|escort) of the same ship and id at X, its age against kArrModeMs + kStepClampMs (905 s), the first traced
# order-4 row to X (ship_trace, traced ships only), and the summary's arr_overwritten / arr_overwritten_cap / ctl_arr_mode maxima.
# Usage: python3 -I return_trips177.py ROWS
import sys, re
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
REACH = 905.0
L = []; ctl = []; tr = {}; summ = {}; arr_rows = 0
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_launch ') and ' capital=1 ' in l: L.append(kv(l))
    elif l.startswith('ship_gate_ctl '): ctl.append(kv(l))
    elif l.startswith('ship_gate_ctl_arr'): arr_rows += 1
    elif l.startswith('ship_trace frame=') and ' order=0x4 ' in l:
        d = kv(l); tr.setdefault((d['obj'], d['tgt']), []).append(int(d['t_ms']) / 1e3)
    elif l.startswith('ship_steering_summary '):
        d = kv(l)
        for k in ('arr_overwritten', 'arr_overwritten_cap', 'ctl_arr_mode'):
            if k in d: summ[k] = max(summ.get(k, 0), int(d[k]))
print('ship_gate_ctl_arr rows', arr_rows, '| summary maxima', summ)
ret = 0
for a in L:
    t = int(a['t_ms']) / 1e3; s = a['ship']; X = a['gate']
    ids = [c['id'] for c in ctl if c['ship'] == s and c.get('why') == 'arrival' and abs(int(c['t_ms']) / 1e3 - t) < 1]
    i = ids[0] if ids else '?'
    nxt = [c for c in ctl if c['ship'] == s and c['id'] == i and int(c['t_ms']) / 1e3 > t and c['to'] in ('committed', 'escort')]
    first = nxt[0] if nxt else None
    back = [c for c in nxt if c['gate'] == X]
    b = back[0] if back else None
    o4 = [x for x in tr.get((s, X), []) if x > t + 1]
    ret += 1 if (b and int(b['t_ms']) / 1e3 - t <= REACH) or (o4 and o4[0] - t <= REACH) else 0
    print('%8.1f %s id=%s %s arr=%s | next board %s | back at arrival gate: %s | traced order4->arrival gate: %s' % (
        t, s, i, a['class'], X,
        '%s %s at %.1f (+%.1f s)' % (first['gate'], first['to'], int(first['t_ms']) / 1e3, int(first['t_ms']) / 1e3 - t) if first else 'none',
        '%s at %.1f (+%.1f s, reach %s)' % (b['to'], int(b['t_ms']) / 1e3, int(b['t_ms']) / 1e3 - t, 'IN' if int(b['t_ms']) / 1e3 - t <= REACH else 'out') if b else 'no',
        '%.1f (+%.1f s)' % (o4[0], o4[0] - t) if o4 else 'none'))
print('capital arrivals', len(L), '| returns to the arrival gate within 905 s', ret)
