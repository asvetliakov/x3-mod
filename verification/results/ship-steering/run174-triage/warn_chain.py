# Run174 Q2/Q3: every engine collision warn (ship_avoid_engine_warn, first row of an episode per ship) for the chosen ships, with
# the warn_pre verdict, the ship's last ship_gate_ctl transition before it (frames back), and the ship_trace row of that transition
# frame (order, obst = our avoidance resolve's obstacle, point (leaf point after our avoidance) vs given (the order's field)).
# Usage: python3 -I warn_chain.py ROWS SHIP [SHIP...]
import sys, re
rows, ships = sys.argv[1], set(sys.argv[2:])
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
ctl = {s: [] for s in ships}; trace = {}; pre = {}; warns = []
for l in open(rows, errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = kv(l)
        if d['ship'] in ships: ctl[d['ship']].append((int(d['frame']), int(d['t_ms']), d['gate'][-4:], d['from'], d['to'], d['why'], d['order']))
    elif l.startswith('ship_trace frame='):
        d = kv(l)
        if d['obj'] in ships: trace[(d['obj'], int(d['frame']))] = d
    elif l.startswith('ship_avoid_engine_warn_pre '):
        d = kv(l)
        if d['obj'] in ships: pre[(d['obj'], int(d['frame']))] = l.split('e_verdict=')[1].split()[0] + ' ' + ' '.join(l.split()[-2:])
    elif l.startswith('ship_avoid_engine_warn '):
        d = kv(l)
        if d['obj'] in ships: warns.append(d)
last = {}
for d in warns:
    s, f = d['obj'], int(d['frame'])
    if last.get(s, -10) >= f - 3: last[s] = f; continue
    last[s] = f
    p = pre.get((s, f - 1), '-')
    c = [x for x in ctl[s] if x[0] <= f]; c = c[-1] if c else None
    tr = trace.get((s, c[0])) if c else None
    tr0 = trace.get((s, f - 2))
    g = lambda t: 'ord=%s obst=%s P!=given=%d' % (t['order'], t['obst'], t['point'] != t['given']) if t else 'no trace'
    print('%s f=%d g=%.1f other=%s class=%s d=%s | pre: %s | last ctl: %s | trace@ctl: %s | trace@f-2: %s' % (
        s[-4:], f, (int(trace[(s, f)]['t_ms']) if (s, f) in trace else 0) / 1000, d['other'], d['code'], d['d_m'], p,
        ('f-%d %s %s>%s %s' % (f - c[0], c[2], c[3], c[4], c[5])) if c else '-', g(tr), g(tr0)))
