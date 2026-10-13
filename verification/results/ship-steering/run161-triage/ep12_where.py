"""Run161 Q1/Q3: where each capital 0x12 episode (episodes12.txt) happened: the capital's position nearest in frame from ship_trace pos
or ship_avoid_detour x_m (within 120 frames), and the nearest E row with B = a gate (d_m, within 300 frames), as |x-G| and s=(x-G).F to each gate in gates.txt, plus the controller role just before.
Usage: python3 -I ep12_where.py rows.txt gates.txt episodes12.txt"""
import sys, re, math, bisect, collections as C
caps = {l.split()[0]: l.split()[1] for l in open('caps.txt')}
gates = [(p[0], [float(x) for x in p[1].split(',')], [float(x) for x in p[2].split(',')], float(p[3])) for p in (l.split() for l in open(sys.argv[2]))]
gd = C.defaultdict(list); pos = C.defaultdict(list); role = C.defaultdict(list)
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        m = re.match(r'ship_trace frame=(\d+) t_ms=\d+ obj=(\S+) class=\S+ order=\S+ tgt=\S+ tgt_class=\d+ pos=(\S+)', l)
        if m and m[2] in caps: pos[m[2]].append((int(m[1]), [int(x) / 500 for x in m[3].split(',')]))
    elif l.startswith('ship_avoid_detour '):
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        if d['obj'] in caps: pos[d['obj']].append((int(d['frame']), [float(x) for x in d['x_m'].split(',')]))
    elif l.startswith('ship_avoid_e_verdict ') and ' B_class=18 ' in l:
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        if d['A'] in caps: gd[d['A']].append((int(d['frame']), d['B'][-4:], float(d['d_m'])))
    elif l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); role[d['ship']].append((int(d['frame']), d['to'], d['why']))
for o in pos: pos[o].sort(key=lambda x: x[0])
for l in open(sys.argv[3]):
    p = l.split()
    if not p or p[0] not in caps: continue
    o = p[0]; f0 = int(p[3].split('-')[0]); dur = p[5]
    P = pos[o]; i = bisect.bisect_left([x[0] for x in P], f0)
    cand = [P[k] for k in (i - 1, i) if 0 <= k < len(P)]
    best = min(cand, key=lambda x: abs(x[0] - f0)) if cand else None
    rl = [x for x in role[o] if x[0] < f0]
    where = 'no position within 120 frames'
    if best and abs(best[0] - f0) <= 120:
        g = min(gates, key=lambda g: math.dist(best[1], g[1]))
        s = sum((a - b) * c for a, b, c in zip(best[1], g[1], g[2]))
        where = 'pos@%d gate %s |x-G| %.0f m (%.1f r) s %.0f m' % (best[0], g[0][-4:], math.dist(best[1], g[1]), math.dist(best[1], g[1]) / g[3], s)
    ge = [x for x in gd[o] if abs(x[0] - f0) <= 300]
    ge = min(ge, key=lambda x: abs(x[0] - f0)) if ge else None
    where += ' | E row to gate: %s' % ('gate %s d %.0f m @%d' % (ge[1], ge[2], ge[0]) if ge else 'none within 300 frames (beyond the gate warn range)')
    # proxy (inferred): any traced capital's position within 120 frames, with its gate distance; the episode ship lies within the
    # warn's d_m of the warn other, not of this proxy, so the band is indicative only
    px = []
    for q, P2 in pos.items():
        if q == o: continue
        j = bisect.bisect_left([x[0] for x in P2], f0)
        for k in (j - 1, j):
            if 0 <= k < len(P2) and abs(P2[k][0] - f0) <= 120:
                g2 = min(gates, key=lambda g: math.dist(P2[k][1], g[1])); px.append('%s %.0f m (%.1f r)' % (q, math.dist(P2[k][1], g2[1]), math.dist(P2[k][1], g2[1]) / g2[3]))
    where += ' | traced capitals then: %s' % ('; '.join(sorted(set(px))) or '-')
    print(o, caps[o], 'start', f0, 'dur', dur, '|', where, '| role before:', rl[-1] if rl else 'never', '|', ' '.join(p[12:16]))
