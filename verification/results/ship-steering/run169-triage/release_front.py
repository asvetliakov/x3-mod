# Run167 Q4 (B.6 item 3): every release with the head's position against the ring-0 front end a1 = zx - kLeadM - R_h (zx ~ the member's
# z at its hand-over row; members without a hand-over take the median zx of the same gate), the window bound a1 + 866 m, nose.F at the
# release row (nearest member row at or before it), the stage before the release, nose at X (transits.txt) and any later
# go-around / re-hold (stage funnel>hold) of the same member. Usage: python3 -I release_front.py rows.txt
import sys, os, re, statistics as ST, collections as C
D = os.path.dirname(os.path.abspath(__file__)); ids = {}; ev = []; mem = C.defaultdict(list)
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids.setdefault(d['ship'], d['id']); ev.append(d)
    elif l.startswith('ship_gate_ctl_member '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); d['p'] = [float(x) for x in d['p_m'].split(',')]; mem[d['ship']].append(d)
zx = {}
for d in ev:
    if d['why'] == 'handover' and d['ship'] not in zx:
        r = [m for m in mem[d['ship']] if abs(int(m['t_ms']) - int(d['t_ms'])) <= 1500]
        if r: zx[(d['ship'], d['gate'])] = min(r, key=lambda m: abs(int(m['t_ms']) - int(d['t_ms'])))['p'][2]
gz = C.defaultdict(list)
for (s, g), z in zx.items(): gz[g].append(z)
nx = {}
for l in open(os.path.join(D, 'transits.txt')):
    p = l.split()
    if p and p[0].isdigit(): nx[p[0]] = l.split('|')[-2].strip()
print('gate id | release game | stage before | zx (src) | z - a1 m (window bound 866) | in front turn | nose.F | nose at X deg | later funnel>hold')
for r in [d for d in ev if d['why'] == 'release']:
    s, g, t = r['ship'], r['gate'], int(r['t_ms'])
    z0, src = (zx[(s, g)], 'own') if (s, g) in zx else (ST.median(gz[g]), 'gate median') if gz[g] else (None, '-')
    rows = [m for m in mem[s] if m['gate'] == g and int(m['t_ms']) <= t]
    m = rows[-1] if rows else None
    a1 = z0 - 2500 - 1000 if z0 is not None else None
    dz = m['p'][2] - a1 if m and a1 is not None else None
    back = [d for d in ev if d['ship'] == s and d['gate'] == g and int(d['t_ms']) > t and d['stage'] == 'funnel>hold']
    print('%s %-6s | %.1f | %s | %s (%s) | %s | %s | %s | %s | %d' % (g[-4:], ids[s], t / 1000, r['stage'], '%.0f' % z0 if z0 is not None else '-', src,
          '%.0f' % dz if dz is not None else '-', 'yes' if dz is not None and dz > 0 else 'no', m['nose_f'] if m else '-', nx.get(ids[s], '-'), len(back)))
