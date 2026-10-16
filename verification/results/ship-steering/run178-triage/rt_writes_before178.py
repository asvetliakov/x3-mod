# Run178 Q1 (the kill criterion's question, per episode): writes of ours on A or B in the 30 s + 2 s (the window's largest step) before
# each 0x12 episode among the four: ship_avoid_detour rows (our avoidance's detour point, logged for every ship), traced rows with
# point != given and obst != 0 (the detour applied), traced rows with emg=1 and des < des_engine (the avoidance emergency's speed at
# site B), lane/member/rewrite/pair/join rows (none expected). Trace coverage of each ship in the window is printed, because the
# trace-only kinds cannot be excluded where a ship was not traced. Usage: python3 -I rt_writes_before178.py ROWS
import sys, re, bisect, collections as C
S = ['0x8fe612b8', '0x8fe58420', '0x39cfa520', '0x39cf2180']; W = 32.0
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
f2t = {}; ev = {s: [] for s in S}; cov = {s: [] for s in S}; warns = []
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        d = kv(l); f = int(d['frame']); t = int(d['t_ms']) / 1e3; f2t.setdefault(f, t); s = d['obj']
        if s in ev:
            cov[s].append(t)
            if d['point'] != d['given'] and d['obst'] != '0x00000000': ev[s].append((t, 'point(obst %s, order %s)' % (d['obst'][-4:], d['order'])))
            if d['emg'] != '0' and int(d['des']) < int(d['des_engine']): ev[s].append((t, 'emg speed des<des_engine (order %s)' % d['order']))
    elif l.startswith('ship_avoid_detour '):
        d = kv(l)
        if d['obj'] in ev: ev[d['obj']].append((int(d['frame']), 'detour_row(obstacle %s kind %s, order %s)' % (d['obstacle'][-4:], d['kind'], d['order'])))
    elif l.startswith(('ship_gate_ctl_rewrite ', 'ship_gate_ctl_pair ', 'ship_gate_capital_join ', 'ship_gate_ctl_defer ')):
        d = kv(l)
        for s in (d.get('ship'), d.get('obj'), d.get('a'), d.get('b')):
            if s in ev: ev[s].append((int(d['frame']), l.split()[0]))
    elif l.startswith('ship_avoid_engine_warn '):
        d = kv(l)
        if d['obj'] in ev and d['other'] in ev: warns.append(d)
fk = sorted(f2t); T = lambda f: f2t[fk[min(bisect.bisect_left(fk, f), len(fk) - 1)]]
for s in S: ev[s] = sorted((x if isinstance(x, float) else T(x), k) for x, k in ev[s])
last = {}
for d in warns:
    s, f, o = d['obj'], int(d['frame']), d['other']
    if last.get(s, -10) >= f - 3: last[s] = f; continue
    last[s] = f; t = T(f)
    print('%.1f A=%s B=%s' % (t, s[-4:], o[-4:]))
    for who, x in (('A', s), ('B', o)):
        c = [u for u in cov[x] if t - W <= u <= t]
        k = C.OrderedDict()
        for u, n in ev[x]:
            if t - W <= u <= t: k.setdefault(n, [u, u]); k[n][1] = u
        print('   %s %s traced %.1f of %.0f s (rows %d) | writes: %s' % (who, x[-4:], (c[-1] - c[0]) if len(c) > 1 else 0, W, len(c),
              '; '.join('%s %.1f-%.1f (-%.1f s)' % (n, a, b, t - b) for n, (a, b) in k.items()) or 'none'))
