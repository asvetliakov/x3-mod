# Run163 Q2: each escort's time from its leader's order-8 launch to its own launch at the gate, split by reason, game and real s.
# Per game second from the ship_gate_ctl_seq rows (the sequencer's candidate, busy_by, window/timing) and the escort's membership state
# from the ship_gate_ctl rows (role from 'to', stage after '>'). Usage: python3 -I sb_wait.py rows.txt gate leader_ship escort_ship...
import sys, os, re, collections as C; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sb_time
a = sys.argv; tm = sb_time.load(a[1]); gate, leader, escs = a[2], a[3], a[4:]
ids = {'0x00000000': '-'}; ev = C.defaultdict(list); seq = []
for l in open(a[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids[d['ship']] = d['id']
        if d['gate'] == gate: ev[d['ship']].append((int(d['frame']), d['to'], d['stage'].split('>')[1], d['why']))
    elif l.startswith('ship_gate_ctl_seq ') and ' gate=' + gate + ' ' in l:
        d = dict(re.findall(r'(\w+)=(\S+)', l)); seq.append(d)
nm = lambda p: ids.get(p, p)
t_l = next(f for f, to, st, why in ev[leader] if why == 'launch')
for e in escs:
    t_e = next(f for f, to, st, why in ev[e] if why == 'launch')
    tot = C.defaultdict(lambda: [0.0, 0.0]); first = {}
    import bisect
    sf = [int(d['frame']) for d in seq]; g0, g1 = tm.game(t_l), tm.game(t_e); t = g0
    while t < g1:  # a 0.5 game-s grid; the seq row counts only when it is at most 1.5 game s old (no rows while nobody holds)
        f = tm.frame_at_game(t); i = bisect.bisect_right(sf, f) - 1
        d = seq[i] if i >= 0 and t - int(seq[i]['t_ms']) <= 1500 else None
        st = [x for x in ev[e] if x[0] <= f]; role, stage = (st[-1][1], st[-1][2]) if st else ('none', 'none')
        if role == 'escort': k = 'escort (reserved key), waiting for the script order 4 [%s]' % stage
        elif role in ('none', 'arrival'): k = 'not a member: reservation lapsed, before its own order 4'
        elif role == 'evicted': k = 'evicted: engine 0x12 vs own gate'
        elif stage in ('funnel', 'handover', 'dock', 'launch'): k = 'own transit: %s' % stage
        elif d is None: k = 'committed hold, no seq row'
        elif d['cand'] == e and d['busy_by'] != '0x00000000': k = 'head, funnel busy by %s' % nm(d['busy_by'])
        elif d['cand'] == e: k = 'head, funnel free, window=%s timing=%s (petal lap)' % (d['window'], d['timing'])
        elif d['busy_by'] != '0x00000000': k = 'behind head %s, funnel busy by %s' % (nm(d['cand']), nm(d['busy_by']))
        else: k = 'behind head %s (strict order), funnel free' % nm(d['cand'])
        dg = min(500, g1 - t) / 1000; f2 = tm.frame_at_game(t + 500); dr = max(0, tm.real(f2) - tm.real(f)) / 1000
        tot[k][0] += dg; tot[k][1] += dr; first.setdefault(k, f); t += 500
    print('== escort %s (id %s): leader %s launch f %d game %.1f real %.1f -> escort launch f %d game %.1f real %.1f: %.1f game s %.1f real s' % (e, nm(e),
          nm(leader), t_l, tm.game(t_l) / 1000, tm.real(t_l) / 1000, t_e, tm.game(t_e) / 1000, tm.real(t_e) / 1000, (tm.game(t_e) - tm.game(t_l)) / 1000,
          (tm.real(t_e) - tm.real(t_l)) / 1000))
    for k, (g, r) in sorted(tot.items(), key=lambda kv: -kv[1][0]): print('%8.1f %6.1f  %s (first f %d)' % (g, r, k, first[k]))
