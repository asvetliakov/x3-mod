"""Run161 Q3/Q4: the residual clunk near gates. For every traced ship (ship_trace), the rows within 6 r_gate of a gate (gates.txt;
not order 5 past the plane) are labelled per row:
  d  order 0x12 (engine evade; the warn's other from ship_avoid_engine_warn)
  a  script: order 0x1f (rotate), order 0/0x1d (KC gap / idle), or the engine itself wants < 20 m/s (des_engine) and ours did not lower
  e  controller yield: ours lowered (des < des_engine) on a capital member while ctl_capped rose in that summary interval
  c  our avoidance: ours lowered with emg=1 or a detour obstacle (obst != 0)
  accel: ours did not lower and the engine wants >= 20 m/s (the ship is accelerating out of an earlier stop; ignored for the
     majority unless alone)
  b  ours otherwise (capital join/lead-in/slow join/fallback; small-ship gate lead, P2/P4)
Events: dip = v < 20 m/s for > 3 game s (consecutive rows, gaps <= 10 frames); rotate = order 0x1f run; reversal = the nose turns
> 120 deg within 10 game s outside 0x1f (cause d when 0x12 is in it or ended <= 30 s before); detour = obst != 0 for > 1 game s; 0x12 = order 0x12 run. Each event's cause = the
label with most game time over its rows (dips, reversals) or its definition. Per-cause totals in game s, per class group, and the
three worst witnesses. Usage: python3 -I clunk.py rows.txt gates.txt [--small]"""
import sys, re, math, bisect, collections as C
sys.path.insert(0, __import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)), '..', '..', '..', 'probe'))
import ship_trace_rows as T
CAP = {'M1', 'M2', 'TL', 'M7', 'M0'}
SMALL = '--small' in sys.argv
gates = []
for l in open(sys.argv[2]):
    p = l.split()
    if len(p) > 3 and p[1] != 'insufficient': gates.append((p[0], [float(x) for x in p[1].split(',')], [float(x) for x in p[2].split(',')], float(p[3])))
KV = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
cls = {}; rows = C.defaultdict(list); warns = C.defaultdict(list); capped = []; mode = C.defaultdict(list)
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        r = T.parse_row(l)
        if not r: continue
        o = '0x%08x' % r['obj']; cls[o] = r['class']
        if (r['class'] in CAP) == SMALL: continue
        rows[o].append(r)
    elif l.startswith('ship_avoid_engine_warn '):
        d = KV(l); warns[d['obj']].append((int(d['frame']), d['other'], d.get('code', '-'), d['class']))
    elif l.startswith('ship_avoid_e_verdict '):
        d = KV(l); cls.setdefault(d['B'], d['B_code'] if d['B_code'] != '-' else 'class' + d['B_class'])
    elif l.startswith('ship_steering_summary '):
        d = KV(l)
        if 'ctl_capped' in d: capped.append((int(d['frame']), int(d['ctl_capped'])))
    elif l.startswith('ship_gate_capital_join '):
        d = KV(l); mode[d['obj']].append((int(d['frame']), d['mode']))
for g in gates: cls[g[0]] = 'gate'
cf = [f for f, _ in capped]
def cap_rose(f):  # ctl_capped rose in the summary interval containing frame f
    i = bisect.bisect_left(cf, f)
    if i <= 0 or i >= len(capped): return False
    return capped[i][1] > capped[i - 1][1]
def jmode(o, f):
    m = mode.get(o)
    if not m: return '-'
    i = bisect.bisect_right([x[0] for x in m], f) - 1
    return m[i][1] if i >= 0 else '-'
def near(p):
    best = None
    for gid, G, F, r in gates:
        d = math.dist(p, G)
        if d <= 6 * r and (best is None or d < best[1]): best = (gid, d, sum((a - b) * c for a, b, c in zip(p, G, F)), r)
    return best
def label(o, r):
    od = r['order']; v = r['v'] / 500; de = r['des_engine'] / 500; ds = r['des'] / 500
    if od == 0x12: return 'd'
    if od == 0x1f or od in (0x0, 0x1d): return 'a'
    if ds < de - 0.5:
        if r.get('emg') == 1 or (r.get('obst') or 0) != 0: return 'c'
        if not SMALL and cap_rose(r['frame']): return 'e'
        return 'b'
    if de < 20: return 'a'
    return 'accel' if v < 20 else '-'  # ours did not lower and the engine wants >= 20 m/s: accelerating after an earlier stop
tot = C.Counter(); n = C.Counter(); wit = []; per_ship = C.defaultdict(C.Counter); obst_tot = C.Counter(); d_other = C.Counter(); prev_o = C.Counter()
def gs(a, b): return (b['t_ms'] - a['t_ms']) / 1e3
for o, rs in rows.items():
    rs.sort(key=lambda r: r['frame'])
    ins = []
    for r in rs:
        p = [x / 500 for x in r['pos']]; nb = near(p)
        if nb and not (r['order'] == 5 and nb[2] > 0): ins.append((r, nb))
    def runs(pred, gap=10):
        out = []; cur = []
        for r, nb in ins:
            if pred(r) and (not cur or r['frame'] - cur[-1][0]['frame'] <= gap): cur.append((r, nb))
            elif pred(r): out.append(cur); cur = [(r, nb)]
            else:
                if cur: out.append(cur); cur = []
        if cur: out.append(cur)
        return out
    def major(seg):
        c = C.Counter()
        for (a, _), (b, _) in zip(seg, seg[1:]): c[label(o, a)] += gs(a, b)
        c2 = C.Counter({k: v for k, v in c.items() if k not in ('accel', '-')})
        return (c2 or c).most_common(1)[0][0] if c else label(o, seg[0][0])
    for seg in runs(lambda r: r['v'] / 500 < 20 and r['order'] not in (0x1f,)):
        dur = gs(seg[0][0], seg[-1][0])
        if dur <= 3: continue
        k = major(seg); ev = 'dip/stop'; tot[(ev, k)] += dur; n[(ev, k)] += 1; per_ship[o][(ev, k)] += dur
        wit.append((dur, o, ev, k, seg[0][0]['frame'], seg[-1][0]['frame'], seg[0][1][0], '%.0f' % seg[0][1][1], jmode(o, seg[0][0]['frame']),
                    'v %.0f->%.0f des %.0f eng %.0f order 0x%x' % (seg[0][0]['v'] / 500, min(x[0]['v'] for x in seg) / 500, seg[len(seg)//2][0]['des'] / 500, seg[len(seg)//2][0]['des_engine'] / 500, seg[0][0]['order'])))
    for seg in runs(lambda r: r['order'] == 0x1f):
        dur = gs(seg[0][0], seg[-1][0]); ev = 'rotate'; tot[(ev, 'a')] += dur; n[(ev, 'a')] += 1; per_ship[o][(ev, 'a')] += dur
        wit.append((dur, o, ev, 'a', seg[0][0]['frame'], seg[-1][0]['frame'], seg[0][1][0], '%.0f' % seg[0][1][1], jmode(o, seg[0][0]['frame']), ''))
    for seg in runs(lambda r: r['order'] == 0x12):
        dur = gs(seg[0][0], seg[-1][0]); ev = '0x12'
        f0 = seg[0][0]['frame']; w = [x for x in warns[o] if f0 - 15 <= x[0] <= f0 + 5]
        oth = (cls.get(w[0][1], 'class' + w[0][3])) if w else 'no warn row'
        tot[(ev, 'd:' + oth)] += dur; n[(ev, 'd:' + oth)] += 1; per_ship[o][(ev, 'd')] += dur; d_other[oth] += dur
        i0 = next(i for i, x in enumerate(ins) if x[0]['frame'] == f0)
        prev = next(('0x%x tgt %s' % (x[0]['order'], cls.get('0x%08x' % x[0]['tgt'], '?')) for x in reversed(ins[max(0, i0 - 300):i0]) if x[0]['order'] not in (0x0, 0x12)), '-')
        prev_o[(SMALL, prev.split()[0], prev.split()[-1] if prev != '-' else '-')] += dur
        wit.append((dur, o, ev, 'd', f0, seg[-1][0]['frame'], seg[0][1][0], '%.0f' % seg[0][1][1], jmode(o, f0), 'other %s %s prev %s' % (w[0][1] if w else '-', oth, prev)))
    for seg in runs(lambda r: (r.get('obst') or 0) != 0, gap=3):
        dur = gs(seg[0][0], seg[-1][0])
        if dur <= 1: continue
        ob = '0x%08x' % seg[0][0]['obst']; oc = cls.get(ob, '?'); oc = 'capital' if oc in CAP else ('station/class' + oc[5:] if oc.startswith('class') else oc)
        tot[('detour', 'c:' + oc)] += dur; n[('detour', 'c:' + oc)] += 1; per_ship[o][('detour', 'c')] += dur; obst_tot[oc] += dur
        wit.append((dur, o, 'detour', 'c', seg[0][0]['frame'], seg[-1][0]['frame'], seg[0][1][0], '%.0f' % seg[0][1][1], jmode(o, seg[0][0]['frame']), 'obstacle %s %s' % (ob, oc)))
    # reversals: nose turns > 120 deg within 10 game s, outside 0x1f
    j = 0; last_end = -1
    for i, (r, nb) in enumerate(ins):
        if r['order'] == 0x1f or r['frame'] <= last_end: continue
        while j < len(ins) and gs(r, ins[j][0]) < 10: j += 1
        for k in range(i + 1, min(j, len(ins))):
            q = ins[k][0]
            if q['order'] == 0x1f: break
            c = sum(a * b for a, b in zip(r['fwd'], q['fwd']))
            if c < math.cos(math.radians(120)):
                seg = ins[i:k + 1]; dur = gs(r, q)
                pre12 = any(x[0]['order'] == 0x12 and 0 <= gs(x[0], r) <= 30 for x in ins[max(0, i - 400):i])
                k2 = 'd' if pre12 or any(x[0]['order'] == 0x12 for x in seg) else major(seg)
                tot[('reversal', k2)] += dur; n[('reversal', k2)] += 1; per_ship[o][('reversal', k2)] += dur
                wit.append((dur, o, 'reversal', k2, r['frame'], q['frame'], nb[0], '%.0f' % nb[1], jmode(o, r['frame']), 'order 0x%x->0x%x v %.0f' % (r['order'], q['order'], r['v'] / 500)))
                last_end = q['frame']; break
# distinct time per cause: each in-band row takes the cause of the first event covering it (0x12, rotate, detour, dip, reversal order)
cov = C.defaultdict(dict)
for w in sorted(wit, key=lambda w: ['0x12', 'rotate', 'detour', 'dip/stop', 'reversal'].index(w[2])):
    for f in range(w[4], w[5] + 1): cov[w[1]].setdefault(f, w[3])
uni = C.Counter()
for o, rs in rows.items():
    for a, b in zip(rs, rs[1:]):
        k = cov[o].get(a['frame'])
        if k and b['frame'] - a['frame'] <= 10: uni[k] += (b['t_ms'] - a['t_ms']) / 1e3
print('ships with rows within 6 r_gate:', sum(1 for o in rows), 'group', 'small' if SMALL else 'capital')
print('== per (event, cause): game s / count')
for k in sorted(tot): print('  %-10s %-22s %8.1f s  %4d' % (k[0], k[1], tot[k], n[k]))
bc = C.Counter()
for (ev, k), v in tot.items(): bc[k[0]] += v
print('== per cause letter (all events, game s):', {k: round(v, 1) for k, v in sorted(bc.items())})
print('== distinct game s per cause (overlaps removed):', {k: round(v, 1) for k, v in sorted(uni.items())})
print('== detour obstacle classes (game s):', {k: round(v, 1) for k, v in obst_tot.most_common()})
print('== 0x12 other (game s):', {k: round(v, 1) for k, v in d_other.most_common()})
print('== 0x12 game s by the order before it (order, target class):', {'%s %s' % k[1:]: round(v, 1) for k, v in prev_o.most_common()})
print('== per ship (game s)')
for o, c in sorted(per_ship.items(), key=lambda kv: -sum(kv[1].values())):
    print('  %s %s %s' % (o, cls.get(o), {'%s:%s' % k: round(v, 1) for k, v in sorted(c.items())}))
print('== worst witnesses (dur game s, ship, event, cause, frames, gate, |x-G| m, join mode, detail)')
for w in sorted(wit, reverse=True)[:25]: print('  %.1f %s %s %s %d-%d %s %s %s %s' % w[:1] + w[1:] if False else '  %.1f %s %s %s %d-%d %s %s %s %s' % w)
