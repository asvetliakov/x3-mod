# Run174 Q5 (convoy_spans.py style, self-contained): per convoy at its departure gate, each ship's first ship_gate_ctl_member row
# with |p_m| <= 15 km (or its arrival launch), its launch (ship_gate_ctl to=none with stage launch), 15 km -> launch, merges and
# merge stops (ship_gate_ctl_merge), stops (member rows v_ms < 15 for >= 3 game s, arrival role excluded), engine warns (0x12)
# with the obstacle and the warn_pre verdict, site-B stop writes (lane rows w=stop) in game s, and turn-away episodes from
# ship_trace (nose > 90 deg from the gate direction at d > 2.5 km before the launch) with the order(s) and lane law(s) in them.
# Usage: python3 -I convoy_spans174.py ROWS
import sys, re, math, bisect, collections as C
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
GATES = {'0x90429090': (-7498, 10774, -57569), '0x90428bb0': (4259, -18037, 31134)}
CONVOYS = [('group 1 pass 1 (event 1)', '0x90429090', ['0x8ffc7c30', '0x8ffced88', '0x904238d0']),
           ('group 2 return (events 2/4)', '0x90429090', ['0x39e43950', '0x90448e28', '0x9045d390']),
           ('group 1 return + pass 2 (events 3/5)', '0x90428bb0', ['0x39e2f3e8', '0x2f1a9290', '0xa9f90b18'])]
ships = {s for _, _, ss in CONVOYS for s in ss}
mem = C.defaultdict(list); lanes = C.defaultdict(list); ctl = C.defaultdict(list); merges = C.defaultdict(list); warns = C.defaultdict(list)
pre = {}; tr = C.defaultdict(list); arr = {}
for l in open(sys.argv[1], errors='replace'):
    if not l.startswith('ship_'): continue
    h = l.split(' ', 1)[0]
    if h == 'ship_gate_ctl_member':
        d = kv(l)
        if d['ship'] in ships: mem[d['ship']].append((int(d['t_ms']), d['gate'], math.dist((0, 0, 0), [float(x) for x in d['p_m'].split(',')]), float(d['v_ms']), d['role'], d['order']))
    elif h == 'ship_gate_ctl_lane':
        d = kv(l)
        if d['ship'] in ships: lanes[d['ship']].append((int(d['t_ms']), d['gate'], d['law'], d['w']))
    elif h == 'ship_gate_ctl':
        d = kv(l)
        if d['ship'] in ships: ctl[d['ship']].append((int(d['t_ms']), d['gate'], d['from'], d['to'], d['why'], d['stage']))
    elif h == 'ship_gate_ctl_merge':
        d = kv(l)
        if d['ship'] in ships: merges[d['ship']].append((int(d['t_ms']), d['event'], d['law'], int(d['dur_ms'])))
    elif h == 'ship_avoid_engine_warn_pre':
        d = kv(l)
        if d['obj'] in ships: pre[(d['obj'], int(d['frame']))] = l.split('e_verdict=')[1].split()[0].split(':')[-1]
    elif h == 'ship_avoid_engine_warn':
        d = kv(l)
        if d['obj'] in ships: warns[d['obj']].append((int(d['frame']), d['other'], d['code'] if d['code'] != '-' else 'gate' if d['class'] == '18' else d['class']))
    elif h == 'ship_trace' and l.startswith('ship_trace frame='):
        d = kv(l)
        if d['obj'] in ships: tr[d['obj']].append((int(d['frame']), int(d['t_ms']), d['order'], [int(x) / 500 for x in d['pos'].split(',')], [float(x) for x in d['fwd'].split(',')]))
    elif h == 'ship_gate_launch':
        d = kv(l)
        if d['ship'] in ships: arr[d['ship']] = int(d['t_ms'])
fr2t = {}
for l in open(sys.argv[1], errors='replace'):
    m = re.match(r'ship_\S+ frame=(\d+) t_ms=(\d+)', l)
    if m: fr2t.setdefault(int(m[1]), int(m[2]))
fk = sorted(fr2t)
def ft(f): i = bisect.bisect_right(fk, f) - 1; return fr2t[fk[max(i, 0)]]
def law_at(s, gate, t):
    v = [x for x in lanes[s] if x[1] == gate]; ts = [x[0] for x in v]; i = bisect.bisect_right(ts, t) - 1
    return (v[i][2], v[i][3]) if i >= 0 else ('-', '-')
for name, gate, ss in CONVOYS:
    print('==', name, 'gate', gate)
    G = GATES[gate]; launches = {}
    for s in ss:
        L = [x[0] for x in ctl[s] if x[1] == gate and x[3] == 'none' and x[5].startswith('launch')]
        launches[s] = L[-1] if L else None
    lead = ss[0]
    for s in ss:
        m = [x for x in mem[s] if x[1] == gate]
        t15 = next((x[0] for x in m if x[2] <= 15000 and x[4] != 'arrival'), None)
        if s in arr and gate == '0x90429090' and 'group 2' in name: t15 = arr[s]
        tl = launches[s]
        stops = []; run = None
        for t, g, dd, v, role, o in m:
            if tl and t > tl: break
            if v < 15 and role != 'arrival':
                run = run or t
            else:
                if run and t - run >= 3000: stops.append((run / 1000, (t - run) / 1000))
                run = None
        if run and m and m[-1][0] - run >= 3000: stops.append((run / 1000, (m[-1][0] - run) / 1000))
        wstop = sum(1 for x in lanes[s] if x[1] == gate and x[3] == 'stop')
        ws = []; lastf = -10
        for f, o, c in warns[s]:
            if f - lastf > 3: ws.append('%.1f:%s(%s)%s' % (ft(f) / 1000, c, o[-4:], pre.get((s, f - 1), '?')))
            lastf = f
        # turn-away episodes before the launch
        eps = []; cur = None
        for f, t, o, p, fw in tr[s]:
            if tl and t > tl: break
            dv = [G[i] - p[i] for i in range(3)]; d = math.hypot(*dv)
            away = d > 2500 and sum(dv[i] * fw[i] for i in range(3)) / d < 0
            if away:
                if not cur: cur = [t, t, set(), set()]
                cur[1] = t; cur[2].add(o); cur[3].add('%s/%s' % law_at(s, gate, t))
            elif cur:
                eps.append(cur); cur = None
        if cur: eps.append(cur)
        mg = [(t / 1000, e, lw, dur / 1000) for t, e, lw, dur in merges[s]]
        print(' %s %s 15km=%s launch=%s 15km->launch=%s' % (s, 'L' if s == lead else 'E', '%.1f' % (t15 / 1000) if t15 else '-', '%.1f' % (tl / 1000) if tl else '-',
              '%.1f' % ((tl - t15) / 1000) if tl and t15 else '-'),
              '| +%.1f s after leader launch' % ((tl - launches[lead]) / 1000) if s != lead and tl and launches[lead] else '')
        print('    stops(v<15,>=3s): %d %s' % (len(stops), ['%.1f+%.1f' % x for x in stops][:12]))
        print('    lane w=stop rows: %d; merges: %d starts, %d finishes %s' % (wstop, sum(1 for x in mg if x[1] == 'start'), sum(1 for x in mg if x[1] == 'finish'),
              ['%.1f %s %s %.1f' % x for x in mg if x[1] == 'finish']))
        print('    0x12 warn episodes: %d %s' % (len(ws), ws))
        o4 = [x for x in mem[s] if x[1] == gate and x[5] == '0x4']
        if s != lead and launches[lead] and o4: print('    first order 4 to the gate: %.1f (+%.1f s after the leader launch)' % (o4[0][0] / 1000, (o4[0][0] - launches[lead]) / 1000))
        print('    turn-away episodes (>90 deg, d>2.5 km, traced frames): %d %s' % (len(eps), ['%.1f-%.1f %s %s' % (a / 1000, b / 1000, sorted(o), sorted(w)) for a, b, o, w in eps if b - a >= 1000]))
    last = max(v for v in launches.values() if v)
    t15L = next((x[0] for x in mem[lead] if x[1] == gate and x[2] <= 15000 and x[4] != 'arrival'), None)
    if 'group 2' in name: t15L = arr[lead]
    print(' span leader 15 km (or arrival) -> last launch: %.1f s game' % ((last - t15L) / 1000))
