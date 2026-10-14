# Run169 Q4: per convoy (leader + escorts; escort->leader edges from ship_gate_ctl_member target), a game-time timeline per ship from the
# leader's first sample inside 15 km of its launch gate (or its first sample) to the last escort launch (or log end): ctl stage changes,
# order changes, stops (v < 15 m/s for >= 3 game s), turn episodes (> 30 deg heading change, gaps <= 3 s merged), detours, and per game
# second "not progressing toward the gate while the gate line is free" (closing < 5 m/s, sequencer busy_by none or self, d_G > 1.5 km,
# not launched) and "nose turned away from the gate" (> 90 deg) by owner bucket, game and real seconds (each ship-second counted once).
# Samples: ship_trace (pos/500 m, v/500 m/s) for traced ships; else the 1 Hz member rows (|p_m| gate-relative, v_ms, heading from the
# displacement). Usage: python3 -I convoy_timeline.py rows.txt [--detail]
import sys, os, re, math, bisect, collections as C; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sb_time
D = os.path.dirname(os.path.abspath(__file__)); rows = sys.argv[1]; detail = '--detail' in sys.argv
GT = {l.split()[0]: [float(x) for x in l.split()[1].split(',')] for l in open(os.path.join(D, 'gates.txt')) if l.startswith('0x') and ',' in l.split()[1]}
tm = sb_time.load(rows)
ids = {'0x00000000': '-'}; ev = C.defaultdict(list); mem = C.defaultdict(list); seq = C.defaultdict(list); det = C.defaultdict(list)
for l in open(rows, errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids.setdefault(d['ship'], d['id'])
        ev[d['ship']].append((int(d['t_ms']), d['gate'], d['from'], d['to'], d['stage'], d['why'], int(d['frame'])))
    elif l.startswith('ship_gate_ctl_member '):
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        mem[d['ship']].append((int(d['t_ms']), d['role'], d['stage'], d['target'], d['gate'], d['class'], [float(x) for x in d['p_m'].split(',')], float(d['v_ms']), d['order'], int(d['frame'])))
    elif l.startswith('ship_gate_ctl_seq '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); seq[d['gate']].append((int(d['t_ms']), d['busy_by'], d['cand'], d['window'], d['timing'], int(d['block_ms'])))
    elif l.startswith('ship_avoid_detour '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); det[d['obj']].append((tm.game(int(d['frame'])), d['order'], d['obstacle'], d['kind'], float(d['miss_m'])))
I = lambda p: ids.get(p, p)
lead_of = C.defaultdict(list)
for s, v in mem.items():
    for r in v:
        if r[1] == 'escort' and r[3] not in GT and r[3] != '0x00000000' and s not in lead_of[r[3]]: lead_of[r[3]].append(s)
ships = set(lead_of) | {e for v in lead_of.values() for e in v}
tr = C.defaultdict(list)
for l in open(rows, errors='replace'):
    if l.startswith('ship_trace frame='):
        m = re.match(r'ship_trace frame=(\d+) t_ms=(\d+) obj=(\S+) class=(\S+) order=(\S+) tgt=\S+ \S+ pos=(\S+) fwd=(\S+) \S+ \S+ v=(-?\d+) des=(-?\d+) .* vmax=(\d+)', l)
        if m and m[3] in ships:
            tr[m[3]].append((int(m[2]), int(m[1]), m[5], [float(x) / 500 for x in m[6].split(',')], [float(x) for x in m[7].split(',')], int(m[8]) / 500, int(m[9]) / 500, int(m[10]) / 500))
ET = {s: [x[0] for x in v] for s, v in ev.items()}; MT = {s: [x[0] for x in v] for s, v in mem.items()}; ST = {g: [x[0] for x in v] for g, v in seq.items()}
def state(s, t):
    i = bisect.bisect_right(ET.get(s, []), t) - 1; return ev[s][i] if i >= 0 else None
def memrow(s, t):
    i = bisect.bisect_right(MT.get(s, []), t) - 1; return mem[s][i] if i >= 0 else None
def seqrow(g, t):
    i = bisect.bisect_right(ST.get(g, []), t) - 1; return seq[g][i] if i >= 0 and t - seq[g][i][0] < 1500 else None
LA = {s: [x[0] for x in v if x[5] == 'launch'] for s, v in ev.items()}
def ang(a, b):
    c = sum(x * y for x, y in zip(a, b)) / max(1e-9, math.hypot(*a) * math.hypot(*b)); return math.degrees(math.acos(max(-1, min(1, c))))
def bucket(s, t, order, g):
    st = state(s, t); m = memrow(s, t); role = st[3] if st else 'none'; stage = st[4].split('>')[1] if st else 'none'
    sq = seqrow(g, t) if g else None; busy = bool(sq and sq[1] not in ('0x00000000', s))
    if order == '0x8' or stage == 'launch': return 'engine: launch', busy
    if stage in ('handover', 'dock') or order == '0x5': return 'engine: order-5 ramp', busy
    if role == 'escort' or (m and m[1] == 'escort' and role == 'none'):
        if not m or m[3] in GT or m[3] == '0x00000000': return 'script: escort, no leader (0x0 gap / re-target)', busy
        if not any(x <= t for x in LA.get(m[3], [])): return 'script: escort before leader launch (leader-first)', busy
        if stage == 'inbound' or m[2] == 'inbound': return 'script: escort straggling inbound after leader launch (follow distance)', busy
        return 'script: escort after leader launch, no order 4 (10 km pass)', busy
    if role == 'committed' and stage == 'hold':
        if busy: return 'funnel: one at a time (occupied)', busy
        if sq and sq[2] == s and sq[5] > 0: return 'controller: head slot block', busy
        if sq and sq[2] == s and sq[3] == '0': return 'controller: head out of window (petal phase)', busy
        if sq and sq[2] not in (s, '0x00000000'): return 'controller: behind head', busy
        return 'controller: hold, other', busy
    if role == 'committed' and stage == 'funnel': return 'controller: funnel approach', busy
    if role == 'committed' and stage == 'inbound': return 'controller: inbound speed law', busy
    return 'unmanaged (%s/%s)' % (role, stage), busy
# per ship per game second samples: t, frame, order, gate, d, closing, nose-to-gate, heading change, v, des
SAM = {}
for s in ships:
    out = []
    if tr[s]:
        sec = C.OrderedDict()
        for x in tr[s]: sec.setdefault(x[0] // 1000, x)
        ks = list(sec)
        for i, k in enumerate(ks):
            t, f, order, p, fw, v, des, vm = sec[k]; st = state(s, t); g = st[1] if st else None
            if g not in GT: continue
            G = GT[g]; d = math.dist(p, G); clo = dh = None
            if i + 1 < len(ks) and ks[i + 1] == k + 1:
                n = sec[ks[i + 1]]; clo = d - math.dist(n[3], G); dh = ang(fw, n[4])
            out.append((t, f, order, g, d, clo, ang(fw, [a - c for a, c in zip(G, p)]), dh, v, des, vm))
    else:
        v_ = mem[s]; ph = None
        for i, r in enumerate(v_):
            t, g, p = r[0], r[4], r[6]; d = math.hypot(*p); clo = dh = None; nose = None
            if i + 1 < len(v_) and v_[i + 1][4] == g and 0 < v_[i + 1][0] - t <= 2000:
                q = v_[i + 1][6]; dt = (v_[i + 1][0] - t) / 1000; clo = (d - math.hypot(*q)) / dt; mv = [b - a for a, b in zip(p, q)]
                if math.hypot(*mv) > 1: nose = ang(mv, [-x for x in p]); dh = ang(ph, mv) / dt if ph else None; ph = mv
            out.append((t, r[9], r[8], g, d, clo, nose if nose is not None else 0, dh, r[7], -1, -1))
    SAM[s] = out
# global classification, each ship-second once
loss = C.defaultdict(C.Counter); lossr = C.defaultdict(float); away = C.defaultdict(C.Counter); awayr = C.defaultdict(float)
for s, out in SAM.items():
    for i, (t, f, order, g, d, clo, nose, dh, v, des, vm) in enumerate(out):
        if clo is None or any(x <= t for x in LA.get(s, [])) or d <= 1500: continue
        b, busy = bucket(s, t, order, g)
        rdt = (tm.real(tm.frame_at_game(t + 1000)) - tm.real(f)) / 1000
        if clo < 5 and not busy: loss[b][I(s)] += 1; lossr[b] += rdt
        if nose > 90: away[b][I(s)] += 1; awayr[b] += rdt
print('id | per convoy: per ship summary; --detail adds the events (+game s / +real s from the convoy start)')
summ = []; crit = []
for L in sorted(lead_of, key=lambda x: (ev[x][0][0] if ev[x] else 0)):
    E = lead_of[L]; members = [L] + E
    gl = next((x[1] for x in ev[L] if x[5] == 'launch'), None) or next((x[1] for x in ev[L] if x[1] in GT), None)
    sl = [x for x in SAM[L] if x[3] == gl] or SAM[L]
    if not sl: print('==', I(L), 'no samples'); continue
    t15 = next((x[0] for x in sl if x[4] <= 15000), sl[0][0]); d15 = next(x[4] for x in sl if x[0] >= t15)
    ends = []
    for e in members:
        la = [x for x in LA.get(e, []) if x >= t15]; ends.append(la[0] if la else None)
    closed = all(ends); t_end = max(x for x in ends if x) if closed else max(x[0] for m_ in members for x in SAM[m_][-1:])
    fc = min([x[0] for m_ in members for x in ev[m_] if x[5] == 'commit' and x[0] >= t15 - 1200000] or [t15])
    r0 = tm.real(tm.frame_at_game(t15))
    cls = next((r[5] for r in mem[L] if r[5] != '?'), '?')
    print('== convoy leader %s (%s) gate %s escorts %s | start (leader 15 km) game %.1f | end %.1f %s | first commit %.1f' % (I(L), cls, gl[-4:], [I(e) for e in E], t15 / 1000, t_end / 1000, 'all launched' if closed else 'OPEN at log end', fc / 1000))
    for s in members:
        out = [x for x in SAM[s] if t15 <= x[0] <= t_end]
        if not out: print('  -- %s: no samples in window' % I(s)); continue
        evs = [(x[0], '%s %s>%s %s%s' % (x[5], x[2], x[3], x[4], '' if x[1] == gl else ' @' + x[1][-4:])) for x in ev[s] if t15 <= x[0] <= t_end]
        po = None; stop0 = None; turn = None; nl = na = 0; trn = []
        for (t, f, order, g, d, clo, nose, dh, v, des, vm) in out:
            if order != po: evs.append((t, 'order %s' % order)); po = order
            if v < 15 and stop0 is None: stop0 = (t, d)
            if v >= 15 and stop0 is not None:
                if t - stop0[0] >= 3000: evs.append((stop0[0], 'STOP v<15 m/s %.0f s at d_G %.1f km' % ((t - stop0[0]) / 1000, stop0[1] / 1000)))
                stop0 = None
            if dh is not None and dh > 1.0:
                if turn is None: turn = [t, 0.0, d, nose, t, nose]
                turn[1] += dh; turn[4] = t; turn[5] = max(turn[5], nose)
            elif turn is not None and t - turn[4] > 3000:
                if turn[1] >= 30:
                    b = bucket(s, turn[0], order, g)[0]; trn.append((turn[1], b))
                    evs.append((turn[0], 'TURN %.0f deg over %.0f s at d_G %.1f km, nose-to-gate %.0f -> max %.0f -> %.0f [%s]' % (turn[1], (turn[4] - turn[0]) / 1000 + 1, turn[2] / 1000, turn[3], turn[5], nose, b)))
                turn = None
        dd = [x for x in det[s] if t15 <= x[0] <= t_end]
        if dd:
            byk = C.Counter((x[1], I(x[2])) for x in dd)
            evs.append((dd[0][0], 'DETOURS %d rows (order, obstacle) %s, first..last %.0f s' % (len(dd), dict(byk.most_common(3)), (dd[-1][0] - dd[0][0]) / 1000)))
        vs = sorted(x[8] for x in out); ws = sorted(x[9] for x in out if x[9] >= 0)
        print('  -- %s %s %s: samples %d, v flown med %.0f (min %.0f), written med %s, stops %d, turns>30 %d (%.0f deg total) | detours %d' % (
            I(s), 'leader' if s == L else 'escort', 'trace' if tr[s] else 'member rows', len(out), vs[len(vs) // 2], vs[0], '%.0f' % ws[len(ws) // 2] if ws else '-',
            sum('STOP' in w for _, w in evs), len(trn), sum(x[0] for x in trn), len(dd)))
        if detail:
            for t, w in sorted(evs): print('     +%7.1f g +%6.1f r  %s' % ((t - t15) / 1000, (tm.real(tm.frame_at_game(t)) - r0) / 1000, w))
    vm = max([x[10] for x in sl] + [1]); vm = vm if vm > 1 else 100.0
    ideal = d15 / vm + len(E) * (3 + 7.5)
    order_ = sorted([(x, m_) for x, m_ in zip(ends, members) if x]) + [(1e18, m_) for x, m_ in zip(ends, members) if not x]
    att = C.Counter(); attr = C.Counter(); smap = {m_: {x[0] // 1000: x for x in SAM[m_]} for m_ in members}
    for k in range(t15 // 1000, t_end // 1000):
        t = k * 1000; nxt = next((m_ for x, m_ in order_ if x > t), None)
        if nxt is None: break
        x = smap[nxt].get(k)
        if x is None: b = 'no sample (untraced gap)'
        else:
            _, f, order, g, d, clo, nose, dh, v, des, vmx = x; b = bucket(nxt, t, order, g)[0]
            if b.startswith('controller: inbound') or b.startswith('controller: funnel') or b.startswith('script: escort straggling'):
                vref = vmx if vmx > 0 else vm
                if clo is not None and clo >= 0.9 * vref: b = 'ideal flight (>= 0.9 v_max toward the gate)'
                elif b.startswith('controller'): b += ' (below 0.9 v_max)'
        att[b] += 1; attr[b] += (tm.real(tm.frame_at_game(t + 1000)) - tm.real(tm.frame_at_game(t))) / 1000
    span_ = (t_end - t15) / 1000
    crit.append('%s %s: span %.0f game s, ideal %.0f, excess %.0f | ' % (I(L), gl[-4:], span_, ideal, span_ - ideal) + '; '.join('%s %d g/%.0f r' % (b, n, attr[b]) for b, n in att.most_common()))
    summ.append((I(L), gl[-4:], [I(e) for e in E], d15 / 1000, vm, ideal, (t_end - t15) / 1000, (tm.real(tm.frame_at_game(t_end)) - r0) / 1000, (t_end - fc) / 1000, closed))
print('== critical-path attribution (user target, 2026-10-14): ideal = leader d15 / vmax + n_esc x 3 s launch gap + n_esc x 7.5 s merge yield')
print('   (merge yield inferred: one 3 s trail spacing at v_max = 3 v_max m opened at a 0.4 v_max speed difference). Every game second of the window is')
print('   charged to the next ship to launch (convoy launch order): flight at >= 0.9 v_max toward the gate = ideal flight, else its bucket.')
for row in crit: print(row)
print('== span per convoy: leader gate escorts | leader d at start km, vmax m/s | ideal game s (d/vmax + n x 10.5 s) | span start->last launch game / real s | first commit->last launch game s | closed')
for x in summ: print('%s %s %s | %.1f %.0f | %.0f | %.0f / %.0f | %.0f | %s' % x)
print('== not progressing toward the gate while the gate line is free (game s, d_G > 1.5 km, not launched; each ship-second once): game s, real s, bucket, per ship')
for b, c in sorted(loss.items(), key=lambda x: -sum(x[1].values())): print('%7d %6.1f %s %s' % (sum(c.values()), lossr[b], b, dict(c.most_common())))
print('== nose turned away from the gate (> 90 deg; same filter, gate busy included): game s, real s, bucket, per ship')
for b, c in sorted(away.items(), key=lambda x: -sum(x[1].values())): print('%7d %6.1f %s %s' % (sum(c.values()), awayr[b], b, dict(c.most_common())))
