# Run165 Q2: every 0.5 game s a capital spent in stage hold (its petal), from the ship_gate_ctl rows (role/stage), the sequencer's
# ship_gate_ctl_seq row of its gate (latest within 1.5 s: cand, busy_by, window, timing, block_ms) and, for escorts, the leader from
# the ship's latest ship_gate_ctl_member row (target). Reasons: escort (no order 4 from the script yet; the leader's stage), funnel busy
# (occupant's stage), behind the head (the head's own reason), head out of window / timing, slot block, EXCESS (head, funnel free,
# window and timing ok, no block). Then every launch (order 8) -> the next release at the gate, and every release's lead over the
# previous occupant's launch. Usage: python3 -I hold_wait.py rows.txt
import sys, os, re, bisect, collections as C; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sb_time
tm = sb_time.load(sys.argv[1]); ids = {'0x00000000': '-'}; ev = C.defaultdict(list); seq = C.defaultdict(list); mem = C.defaultdict(list)
rel = C.defaultdict(list); lau = C.defaultdict(list)
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids.setdefault(d['ship'], d['id']); t = int(d['t_ms'])
        ev[d['ship']].append((t, d['gate'], d['to'], d['stage'].split('>')[1], d['why'], int(d['frame'])))
        if d['why'] == 'release': rel[d['gate']].append((t, d['ship'], int(d['frame'])))
        if d['why'] == 'launch': lau[d['gate']].append((t, d['ship'], int(d['frame'])))
    elif l.startswith('ship_gate_ctl_seq '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); seq[d['gate']].append(d)
    elif l.startswith('ship_gate_ctl_member '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); mem[d['ship']].append((int(d['t_ms']), d['target'], d['stage'], d['role']))
nm = lambda p: ids.get(p, p)
ST = {g: [int(d['t_ms']) for d in v] for g, v in seq.items()}; MT = {s: [x[0] for x in v] for s, v in mem.items()}
def state(s, t):
    st = [x for x in ev[s] if x[0] <= t]; return st[-1] if st else None
def seqrow(g, t):
    i = bisect.bisect_right(ST.get(g, []), t) - 1
    return seq[g][i] if i >= 0 and t - int(seq[g][i]['t_ms']) <= 1500 else None
def leader(s, t):
    i = bisect.bisect_right(MT.get(s, []), t) - 1
    return mem[s][i][1] if i >= 0 else None
def why_hold(s, t, depth=0):
    x = state(s, t); gate, role, stage = x[1], x[2], x[3]
    if role == 'escort':
        L = leader(s, t); ls = state(L, t) if L in ev else None
        return 'escort waiting for order 4 (leader %s %s)' % ('gate' if L == gate else nm(L), ('%s/%s' % (ls[2], ls[3])) if ls else '-')
    d = seqrow(gate, t)
    if d is None: return 'committed hold, no seq row'
    if d['busy_by'] != '0x00000000':
        o = state(d['busy_by'], t); return 'funnel busy (occupant %s)' % (o[3] if o else '?')
    if d['cand'] != s:
        if d['cand'] == '0x00000000': return 'no candidate (seq cand -)'
        if depth: return 'head %s itself waiting' % nm(d['cand'])
        return 'behind head %s, funnel free: head %s' % (nm(d['cand']), why_hold(d['cand'], t, 1))
    if int(float(d['block_ms'])) > 0: return 'head, slot block (60 s)'
    if d['window'] != '1': return 'head, out of window (petal phase)'
    if d['timing'] != '1': return 'head, timing'
    return 'EXCESS head, funnel free, window/timing ok'
ALL = C.defaultdict(lambda: [0.0, 0.0, 0]); W = []
print('== per capital: hold seconds by reason (game s, real s)')
for s in sorted(ev, key=lambda s: ev[s][0][0]):
    segs = []; rows = ev[s]
    for i, x in enumerate(rows):
        if x[3] == 'hold':
            t1 = rows[i + 1][0] if i + 1 < len(rows) else ev[s][-1][0]
            segs.append((x[0], t1))
    if not segs: continue
    tot = C.defaultdict(lambda: [0.0, 0.0])
    for a, b in segs:
        t = a
        while t < b:
            k = why_hold(s, t); dg = min(500, b - t) / 1000
            dr = max(0, tm.real(tm.frame_at_game(t + 500)) - tm.real(tm.frame_at_game(t))) / 1000 * dg / 0.5
            tot[k][0] += dg; tot[k][1] += dr; ALL[k][0] += dg; ALL[k][1] += dr; ALL[k][2] += 1
            if k.startswith('EXCESS') or 'behind head' in k and 'EXCESS' in k: W.append((s, t, tm.frame_at_game(t)))
            t += 500
    g = sum(v[0] for v in tot.values()); r = sum(v[1] for v in tot.values())
    print('%s id %s: hold %.1f game s %.1f real s' % (s, nm(s), g, r))
    for k, (a, b) in sorted(tot.items(), key=lambda kv: -kv[1][0]): print('   %7.1f %6.1f  %s' % (a, b, k))
print('== ALL capitals')
for k, (a, b, n) in sorted(ALL.items(), key=lambda kv: -kv[1][0]): print('%8.1f %6.1f  %s' % (a, b, k))
print('EXCESS samples', len(W), [(nm(s), t / 1000, f, round(tm.real(f) / 1000, 1)) for s, t, f in W[:20]])
print('== per gate: each release, the previous occupant\'s launch (order 8) and the gap launch -> release (negative = pipelined)')
for g in rel:
    L = sorted(lau[g])
    for t, s, f in sorted(rel[g]):
        prev = [x for x in L if x[0] <= t]; pl = prev[-1] if prev else None
        prv = [x for x in sorted(rel[g]) if x[0] < t]; pr = prv[-1] if prv else None
        pr_launch = next((x for x in L if pr and x[1] == pr[1] and x[0] >= pr[0]), None)
        gap = (t - pr_launch[0]) / 1000 if pr_launch else None
        gr = (tm.real(f) - tm.real(pr_launch[2])) / 1000 if pr_launch else None
        print('gate %s release %-5s game %8.1f real %5.1f | prev released %-5s launched %s | launch->release %s game s %s real s' % (g[-4:], nm(s), t / 1000,
              tm.real(f) / 1000, nm(pr[1]) if pr else '-', '%.1f' % (pr_launch[0] / 1000) if pr_launch else '-', '%.1f' % gap if gap is not None else '-',
              '%.1f' % gr if gr is not None else '-'))
