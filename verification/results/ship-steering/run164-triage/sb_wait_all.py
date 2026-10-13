# Run164 Q2: every member's waiting time from its commit / escort entry to its release (or the log end), split by reason per 0.5 game s:
# its membership state (ship_gate_ctl rows: role, stage), its leader's stage (escorts), and for a committed holder the sequencer's
# ship_gate_ctl_seq row that second (candidate, busy_by and the occupant's stage, window/timing, block_ms > 0 = the 60 s slot block).
# Totals per reason (game s, real s) per member and over all members; 'excess' = a committed holder that is the head (or behind no one)
# with the funnel free and no block. Usage: python3 -I sb_wait_all.py rows.txt
import sys, os, re, bisect, collections as C; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sb_time
tm = sb_time.load(sys.argv[1]); ids = {'0x00000000': '-'}; ev = C.defaultdict(list); seq = C.defaultdict(list); lead = {}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids[d['ship']] = d['id']
        ev[d['ship']].append((int(d['frame']), d['gate'], d['to'], d['stage'].split('>')[1], d['why']))
    elif l.startswith('ship_gate_ctl_seq '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); seq[d['gate']].append(d)
nm = lambda p: ids.get(p, p)
LEAD = {'0x8e03e7e0': '0x8e037b68', '0x8e049da0': '0x8e037b68', '0x8e043bf8': '0x3837fb20', '0x8e082750': '0x3837fb20',
        '0xaa68cfb0': '0x8e0744a0', '0xaa68fe00': '0x8e0744a0'}  # convoys (ctl_rows.txt: escort rows at the leader's commit)
def state(s, f):
    st = [x for x in ev[s] if x[0] <= f]; return st[-1] if st else None
end_f = max(tm.rf); ALL = C.defaultdict(lambda: [0.0, 0.0]); W = []
for s in sorted(ev, key=lambda s: ev[s][0][0]):
    rows = ev[s]; f0 = next((x[0] for x in rows if x[4] in ('commit', 'escort')), None)
    if f0 is None: continue
    rel = next((x[0] for x in rows if x[0] >= f0 and x[4] in ('release', 'rejoin')), end_f)
    # the last release (a member evicted after a release re-enters): wait until the final release
    rels = [x[0] for x in rows if x[4] in ('release',)]; rel = rels[-1] if rels else end_f
    tot = C.defaultdict(lambda: [0.0, 0.0]); first = {}; t = tm.game(f0); g1 = tm.game(rel)
    while t < g1:
        f = tm.frame_at_game(t); x = state(s, f); gate, role, stage = x[1], x[2], x[3]
        sq = seq[gate]; sf = [int(d['frame']) for d in sq]; i = bisect.bisect_right(sf, f) - 1
        d = sq[i] if i >= 0 and t - int(sq[i]['t_ms']) <= 1500 else None
        if role == 'escort':
            L = LEAD.get(s); ls = state(L, f) if L else None
            k = 'escort %s, leader %s' % (stage, (ls[3] if ls and ls[2] == 'committed' else (ls[2] if ls else '?')))
        elif role == 'evicted': k = 'evicted (0x12)'
        elif role == 'committed' and stage == 'inbound': k = 'committed inbound (beyond ring + 2 km)'
        elif role == 'committed' and stage in ('funnel', 'handover', 'dock', 'launch'): k = 'own transit %s' % stage
        elif role == 'committed' and stage == 'hold':
            if d is None: k = 'hold, no seq row'
            elif d['busy_by'] != '0x00000000':
                o = state(d['busy_by'], f); k = 'hold, funnel busy by %s (%s)' % ('other' if d['busy_by'] != s else 'self', o[3] if o else '?')
            elif d['cand'] != s: k = 'hold, behind head %s, funnel free' % nm(d['cand'])
            elif int(d['block_ms']) > 0: k = 'hold, head, slot block (60 s)'
            else: k = 'EXCESS hold, head, funnel free, window=%s timing=%s' % (d['window'], d['timing'])
        else: k = '%s %s' % (role, stage)
        f2 = tm.frame_at_game(t + 500); dg = min(500, g1 - t) / 1000; dr = max(0, tm.real(f2) - tm.real(f)) / 1000
        tot[k][0] += dg; tot[k][1] += dr; ALL[k][0] += dg; ALL[k][1] += dr; first.setdefault(k, f)
        if k.startswith('EXCESS'): W.append((s, f, k))
        t += 500
    print('== %s id %s: entry f %d game %.1f real %.1f -> release f %d game %.1f real %.1f: %.1f game s %.1f real s' % (s, nm(s), f0,
          tm.game(f0) / 1000, tm.real(f0) / 1000, rel, g1 / 1000, tm.real(rel) / 1000, (g1 - tm.game(f0)) / 1000, (tm.real(rel) - tm.real(f0)) / 1000))
    for k, (g, r) in sorted(tot.items(), key=lambda kv: -kv[1][0]): print('%8.1f %6.1f  %s (first f %d)' % (g, r, k, first[k]))
print('== ALL members')
for k, (g, r) in sorted(ALL.items(), key=lambda kv: -kv[1][0]): print('%8.1f %6.1f  %s' % (g, r, k))
print('EXCESS samples (0.5 game s each):', len(W), W[:5])
