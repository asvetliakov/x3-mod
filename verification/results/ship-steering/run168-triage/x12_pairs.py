# Run168 Q8: the convoy-capital 0x12 evictions at 6db0. Per episode (first 0x12 trace frame of the ships), in [t0 - 10 s, t0 + 0.5 s]:
# pair rows between the ships, E verdict counts between them, engine warn rows (obj got 0x12, other), trace v/des/order samples, ship_gate_ctl
# transitions; then after the eviction: the ship's next ship_gate_ctl rows (rejoin), 0x12 span end, commit/release/launch.
# Usage: python3 -I x12_pairs.py rows.txt
import sys, re, collections as C
SH = {'0x8e8b0408': '4248', '0x8e897448': '4454', '0xb474e090': '7087'}
EP = [('ep1 inbound', 25206844 - 0, ('0x8e8b0408', '0x8e897448', '0xb474e090')), ('ep2 hold entry', 25715253, ('0x8e8b0408', '0x8e897448', '0xb474e090'))]
pair = []; ev = []; warn = []; tr = C.defaultdict(list); ctl = C.defaultdict(list); fr_t = {}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace '):
        m = re.match(r'ship_trace frame=(\d+) t_ms=(\d+) obj=(\S+) \S+ order=(\S+) tgt=(\S+).* v=(\S+) des=(\S+) des_engine=(\S+)', l)
        if m: fr_t.setdefault(int(m[1]), int(m[2]))
        if m and m[3] in SH: tr[m[3]].append((int(m[2]), int(m[1]), m[4], float(m[6]), float(m[7]), float(m[8])))
    elif l.startswith('ship_gate_ctl_pair '):
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        if d['a'] in SH and d['b'] in SH: pair.append(d)
    elif l.startswith('ship_avoid_e_verdict '):
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        if d['A'] in SH and d['B'] in SH: ev.append(d)
    elif l.startswith('ship_avoid_engine_warn '):
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        if d['obj'] in SH: warn.append(d)
    elif l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        if d['ship'] in SH: ctl[d['ship']].append(d)
T = lambda d: int(d['t_ms']) if 't_ms' in d else fr_t.get(int(d['frame']), 0)
for name, t0, ships in EP:
    a, b = t0 - 10000, t0 + 500
    print('== %s t0 %.3f' % (name, t0 / 1000))
    for d in pair:
        if a <= T(d) <= b: print('  pair %+.2f %s-%s kind %s verdict %s before %s why %s t_c_s %s yielder %s cap_ms %s d_m %s' % ((T(d) - t0) / 1000, SH[d['a']], SH[d['b']], d['kind'], d['verdict'], d['before'], d['why'], d['t_c_s'], d['yielder'], d['cap_ms'], d['d_m']))
    last = [d for d in pair if T(d) < a and {d['a'], d['b']} == {'0x8e8b0408', '0x8e897448'}]
    if last: d = last[-1]; print('  last 4248-4454 pair row before window: %+.1f s kind %s verdict %s why %s d_m %s' % ((T(d) - t0) / 1000, d['kind'], d['verdict'], d['why'], d['d_m']))
    c = C.Counter((SH[d['A']], SH[d['B']], d['A_order'], d['verdict']) for d in ev if a <= T(d) <= b); print('  E verdicts:', dict(c))
    for d in warn:
        if a <= T(d) <= b: print('  warn %+.2f A(obj, got 0x12) %s B(other) %s d_m %s r_m %s' % ((T(d) - t0) / 1000, SH[d['obj']], SH.get(d['other'], d['other']), d['d_m'], d['r_m']))
    for s in ships:
        X = [x for x in tr[s] if a <= x[0] <= b]; out = []
        for k in (-10, -5, -2, -1, -0.3, 0):
            x = min(X, key=lambda x: abs(x[0] - (t0 + k * 1000))) if X else None
            if x: out.append('%+g: %s v %.0f des %.0f eng %.0f' % (k, x[2], x[3], x[4], x[5]))
        f12 = next(((x[0] - t0) / 1000 for x in tr[s] if x[0] >= a and x[2] == '0x12'), None)
        print('  %s first 0x12 %s | %s' % (SH[s], '%+.2f' % f12 if f12 is not None else '-', ' | '.join(out)))
        for d in ctl[s]:
            if a <= int(d['t_ms']) <= b: print('    ctl %+.2f %s>%s why %s stage %s petal %s' % ((int(d['t_ms']) - t0) / 1000, d['from'], d['to'], d['why'], d['stage'], d['petal']))
        nxt = [d for d in ctl[s] if b < int(d['t_ms'])][:4]
        print('    after:', '; '.join('%+.1f %s>%s %s %s' % ((int(d['t_ms']) - t0) / 1000, d['from'], d['to'], d['why'], d['stage']) for d in nxt))
