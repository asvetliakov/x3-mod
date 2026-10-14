# Run165 Q1/Q2: the ship_gate_ctl_member rows (B.4 item 2, 1 Hz per board member). Per member (ship pointer while its ship_gate_ctl id
# holds; rows after its order-8 launch are counted as post-launch/stale: the board keeps the slot, the pointer may be freed or reused):
# stage segments (role, stage, orders, game/real span, rows), and per hold segment the petal: laps (nose.F < -0.9 then > 0.9), the
# radius about the segment's centroid (gate frame), speed median/range, distance to the gate centre. Then per gate the board count per
# second: live pair-role members (st_age <= 5 s, before launch) vs all rows, and the holders at once. Usage: python3 -I members.py rows.txt
import sys, os, re, math, statistics as ST, collections as C; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sb_time
tm = sb_time.load(sys.argv[1]); M = C.defaultdict(list); launch = {}; ids = {}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids.setdefault(d['ship'], d['id'])
        if d['why'] == 'launch': launch[d['ship']] = int(d['t_ms'])
    elif l.startswith('ship_gate_ctl_member '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); d['p'] = [float(x) for x in d['p_m'].split(',')]; M[d['ship']].append(d)
G = lambda d: int(d['t_ms']) / 1000; R = lambda d: tm.real(int(d['frame'])) / 1000
post = C.Counter(); stale_cls = C.Counter()
print('== per member: stage segments (game s / real s); hold segments with petal laps, radius, speed')
for s in sorted(M, key=lambda s: int(M[s][0]['frame'])):
    rows = M[s]; L = launch.get(s); live = [d for d in rows if L is None or int(d['t_ms']) <= L + 1000]
    post[s] = len(rows) - len(live); stale_cls[s] = C.Counter(d['class'] for d in rows if d not in live)
    if not live: continue
    print('%s id %s class %s gate %s rows %d (post-launch %d, classes then %s)' % (s, ids.get(s, '?'), live[0]['class'], live[0]['gate'][-4:],
          len(rows), post[s], dict(stale_cls[s])))
    seg = []
    for d in live:
        k = (d['gate'][-4:], d['role'], d['stage'])
        if seg and seg[-1][0] == k and int(d['t_ms']) - int(seg[-1][1][-1]['t_ms']) < 5000: seg[-1][1].append(d)
        else: seg.append((k, [d]))
    for k, ds in seg:
        g0, g1, r0, r1 = G(ds[0]), G(ds[-1]), R(ds[0]), R(ds[-1]); od = sorted(set(d['order'] for d in ds))
        line = '  %s %-9s %-8s game %8.1f +%6.1f real %5.1f +%5.1f rows %4d orders %s' % (k[0], k[1], k[2], g0, g1 - g0, r0, r1 - r0, len(ds), ','.join(od))
        v = [float(d['v_ms']) for d in ds]; dg = [math.sqrt(sum(x * x for x in d['p'])) for d in ds]
        if k[2] == 'hold' and len(ds) > 3:
            st = None; laps = 0; t_l = []
            for d in ds:
                nf = float(d['nose_f'])
                if st is None: st = 'f' if nf > 0 else 'b'
                if st == 'f' and nf < -0.9: st = 'b'
                elif st == 'b' and nf > 0.9: st = 'f'; laps += 1; t_l.append(G(d))
            c = [ST.fmean(d['p'][i] for d in ds) for i in range(3)]
            rad = ST.fmean(math.dist(d['p'], c) for d in ds); pt = sorted(set(d['petal'] for d in ds))
            per = ['%.0f' % (b - a) for a, b in zip(t_l, t_l[1:])]
            Q = [[d['p'][i] - c[i] for i in range(3)] for d in ds]; cr = lambda a, b: [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
            nv = [sum(cr(a, b)[i] for a, b in zip(Q, Q[1:])) for i in range(3)]; nn = math.sqrt(sum(x * x for x in nv)) or 1.0; nv = [x / nn for x in nv]
            ang = sum(math.atan2(sum(x * y for x, y in zip(nv, cr(a, b))), sum(x * y for x, y in zip(a, b))) for a, b in zip(Q, Q[1:]))
            path = sum(math.dist(a['p'], b['p']) for a, b in zip(ds, ds[1:]))
            line += ' | petal %s turns %.2f (path %.1f km = %.2f racetrack laps of 9.28 km) nose-laps %d (lap s %s) radius %.0f m centre %.0f,%.0f,%.0f v med %.0f (%.0f..%.0f) d_G %.0f..%.0f' % (
                ','.join(pt), abs(ang) / 2 / math.pi, path / 1000, path / 9283, laps, '/'.join(per) or '-', rad, c[0], c[1], c[2], ST.median(v), min(v), max(v), min(dg), max(dg))
        else: line += ' | v %.0f..%.0f d_G %.0f..%.0f' % (min(v), max(v), min(dg), max(dg))
        print(line)
print('\n== board per gate: per game second, live pair-role members before their launch (st_age <= 5 s) and holders at once')
B = C.defaultdict(lambda: C.defaultdict(set)); H = C.defaultdict(lambda: C.defaultdict(set)); A = C.defaultdict(lambda: C.defaultdict(set))
for s, rows in M.items():
    L = launch.get(s)
    for d in rows:
        sec = int(d['t_ms']) // 1000; A[d['gate']][sec].add(s)
        if (L is None or int(d['t_ms']) <= L + 1000) and 0 <= int(d['st_age_ms']) <= 5000 and d['role'] in ('committed', 'escort'):
            B[d['gate']][sec].add(s)
            if d['stage'] == 'hold': H[d['gate']][sec].add(s)
RS = {}
for rows in M.values():
    for d in rows: RS.setdefault(int(d['t_ms']) // 1000, None)
for sec in RS: f = tm.frame_at_game(sec * 1000); RS[sec] = (tm.real(tm.frame_at_game(sec * 1000 + 1000)) - tm.real(f)) / 1000
for g in sorted(B):
    for name, X in (('live committed/escort', B[g]), ('holders', H[g]), ('all member rows', A[g])):
        hist = C.Counter(len(v) for v in X.values()); mx = max(hist) if hist else 0
        first = min((sec for sec, v in X.items() if len(v) == mx), default=0)
        print('gate %s %-22s per-second histogram %s; max %d first at game %d (%s)' % (g[-4:], name, dict(sorted(hist.items())), mx, first,
              ','.join(sorted(ids.get(x, x) for x in X[first]))))
    k5 = sorted(sec for sec, v in H[g].items() if len(v) >= 5)
    for th in (3, 4):
        ks = sorted(sec for sec, v in H[g].items() if len(v) >= th)
        if ks: print('gate %s seconds with >= %d holders: %d game s, real %.1f s (game %d..%d)' % (g[-4:], th, len(ks), sum(RS.get(x, 0) for x in ks), ks[0], ks[-1]))
    if k5: print('gate %s seconds with >= 5 holders: %d (game %d..%d)' % (g[-4:], len(k5), k5[0], k5[-1]))
print('\npost-launch member rows (stale slots; class read through the old pointer):', {ids.get(s, s): n for s, n in post.items() if n},
      'max st_age_ms', max(int(d['st_age_ms']) for rows in M.values() for d in rows))
