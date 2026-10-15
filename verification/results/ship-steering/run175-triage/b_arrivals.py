# Run175 B Q3: every capital arrival (ship_gate_ctl to=arrival): the order chain after it (member/trace/e_verdict A_order rows, first 6
# changes within 120 game s), the ctl transitions of that ship within 120 s, site E verdicts with the ship as A against its arrival gate
# (verdict counts), engine 0x12 by or against it within 600 s, member rows with law=hold/w=stop at the arrival gate, and plane crossings of
# the arrival gate from ship_trace (sign changes of s = (pos - G).F with lateral < 3 r_gate) until the ship's next launch/gone or 600 s.
# Usage: python3 -I b_arrivals.py ROWS
import sys, re, math, collections as C
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
GF = {}; arr = []; ctl = C.defaultdict(list); orders = C.defaultdict(list); ev = C.defaultdict(C.Counter); warns = []; tr = C.defaultdict(list)
lanew = C.defaultdict(C.Counter); tms = 0; RG = 1104.5
for l in open(sys.argv[1], errors='replace'):
    if not l.startswith('ship_'): continue
    m = re.search(r' t_ms=(\d+)', l)
    if m: tms = int(m[1])
    h = l.split(' ', 1)[0]
    if h == 'ship_steering_lane_summary' and 'gates_gate:F:G:r=' in l:
        for e in l.split('gates_gate:F:G:r=')[1].split()[0].split('|'):
            g, f, gg, rr = e.split(':'); GF.setdefault(g, ([float(x) for x in f.split(',')], [float(x) for x in gg.split(',')]))
    elif h == 'ship_gate_ctl':
        d = kv(l); ctl[d['ship']].append((int(d['t_ms']), d['gate'], d['from'], d['to'], d['why'], d['order']))
        if d['to'] == 'arrival': arr.append((int(d['t_ms']), d['ship'], d['gate']))
    elif h == 'ship_gate_ctl_member':
        d = kv(l); orders[d['ship']].append((int(d['t_ms']), d['order'], 'member'))
    elif h == 'ship_gate_ctl_lane':
        d = kv(l); lanew[(d['ship'], d['gate'])][d['law'] + '/' + d['w']] += 1
    elif h == 'ship_trace' and l.startswith('ship_trace frame='):
        d = kv(l); tr[d['obj']].append((int(d['t_ms']), [int(x) / 500 for x in d['pos'].split(',')], d['order']))
        orders[d['obj']].append((int(d['t_ms']), d['order'], 'trace'))
    elif h == 'ship_avoid_e_verdict':
        d = kv(l); orders[d['A']].append((tms, d['A_order'], 'e_verdict')); ev[(d['A'], d['B'])][d['verdict'] + ('/' + d['gate_row'] if 'gate_row' in d else '')] += 1
    elif h == 'ship_avoid_engine_warn':
        d = kv(l); warns.append((tms, d['obj'], d['other'], d['class']))
for t0, s, g in arr:
    F, G = GF[g]
    print('== %s arrives %s at %.1f' % (s, g[-4:], t0 / 1000))
    ch = []; prev = None
    for t, o, src in sorted(orders[s]):
        if t < t0 - 2000 or t > t0 + 120000: continue
        if o != prev: ch.append('%.1f:%s(%s)' % (t / 1000, o, src[0])); prev = o
    print('   orders(120 s):', ' '.join(ch[:8]))
    print('   ctl(120 s):', ['%.1f %s %s>%s %s %s' % (t / 1000, gg[-4:], a, b, w, o) for t, gg, a, b, w, o in ctl[s] if t0 <= t <= t0 + 120000])
    print('   e_verdict vs its gate:', dict(ev[(s, g)]))
    ws = [(t, a, b, c) for t, a, b, c in warns if (a == s or b == s) and t0 <= t <= t0 + 600000]
    eps = []; last = {}
    for t, a, b, c in ws:
        if t - last.get((a, b), -1e9) > 3000: eps.append('%.1f %s->%s(cls %s)' % (t / 1000, a[-4:], b[-4:], c))
        last[(a, b)] = t
    print('   engine warn episodes by/against it (600 s):', len(eps), eps[:10])
    print('   lane law/w at the arrival gate:', dict(lanew[(s, g)]))
    end = next((t for t, gg, a, b, w, o in ctl[s] if t > t0 and b == 'none' and w in ('gone', 'order') and a == 'committed'), t0 + 600000)
    pts = [(t, p) for t, p, o in tr[s] if t0 <= t <= min(end, t0 + 600000)]
    cross = 0; sp = None; mind = 1e9
    for t, p in pts:
        r = [p[i] - G[i] for i in range(3)]; sF = sum(r[i] * F[i] for i in range(3)); lat = math.sqrt(max(0, sum(x * x for x in r) - sF * sF))
        mind = min(mind, math.dist(p, G))
        if sp is not None and (sp > 0) != (sF > 0) and lat < 3 * RG: cross += 1
        sp = sF
    print('   traced frames %d, plane crossings near the gate (lat < 3 r_gate) %d, min dist to its gate %.0f m' % (len(pts), cross, mind if pts else -1))
