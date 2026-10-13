"""Run159 triage: per traced capital, the phase timeline of its gate approach (extends capital_timeline.py, does not replace it).
Usage: python3 -I capital_phases.py <extracted rows (rows.sh) or x3m.log> [--min-s 2]
Phase label per ship_trace row: order 4 -> the join mode of the latest ship_gate_capital_join row (out/join/slow_join/fallback),
suffixed '+floor' when site B's des <= 1 unit/s (the sequencing/follow cap floor, kFollowFloorMs); 0x1f rotate; 0x12 evade;
5 launch leg; 0 transient. Consecutive equal labels form a segment (game time from t_ms; real time from frame_end elapsed_ms).
Per segment: s = (x - G).F (m, G the gate origin, F the gate axis as in capital_timeline.py), lateral offset e to the own grid
lane P (m), heading error to F, v, the summed heading rotation (sum of the angle between consecutive fwd) and the net
displacement. Per order-4 segment: the ship_capital_lane rows of A in it, per neighbour B: verdict/route counts, bind=1 rows,
ds (B ahead > 0) min..max, seq_cap min, A's own e_m/theta_deg (median).
Units: 500 units = 1 m."""
import math, os, re, sys, statistics as St
from collections import Counter, defaultdict
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'probe'))
import ship_trace_rows as T
CAP = {'M1', 'M2', 'TL', 'M7', 'M0'}
MIN_S = float(sys.argv[sys.argv.index('--min-s') + 1]) if '--min-s' in sys.argv else 2.0
def sub(a, b): return [x - y for x, y in zip(a, b)]
def dot(a, b): return sum(x * y for x, y in zip(a, b))
def nrm(a): return math.sqrt(dot(a, a))
def unit(a): n = nrm(a); return [x / n for x in a]
def cross(a, b): return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
def ang(u, v): return math.degrees(math.acos(max(-1, min(1, dot(u, v) / (nrm(u) * nrm(v) or 1)))))
M = lambda p: [x / 500 for x in p]
KV = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
rows = defaultdict(list); small4 = defaultdict(Counter); small5 = defaultdict(set); joins = defaultdict(list); lanes = defaultdict(list); fe = {}
with open(sys.argv[1], errors='replace') as f:
    for l in f:
        if l.startswith('ship_trace frame='):
            r = T.parse_row(l)
            if not r: continue
            if r['class'] in CAP: rows[r['obj']].append(r)
            elif r['tgt_class'] == 18 and r['order'] == 4: small4[r['tgt']][r['given']] += 1
            elif r['tgt_class'] == 18 and r['order'] == 5: small5[r['tgt']].add(r['given'])
        elif l.startswith('ship_gate_capital_join '):
            d = KV(l); joins[int(d['obj'], 16)].append((int(d['frame']), d['mode']))
        elif l.startswith('ship_capital_lane '):
            d = KV(l); lanes[int(d['A'], 16)].append(d)
        elif l.startswith('frame_end '):
            d = KV(l); fe[int(d['frame'])] = int(d['elapsed_ms'])
def gate_frame(rs, g):
    o5 = [r for r in rs if r['order'] == 5 and r['tgt'] == g]
    if not o5 or g not in small4: return None
    G = M(o5[-1]['given']); As = M(small4[g].most_common(1)[0][0]); Bs = [M(b) for b in small5.get(g, ())]
    best = None
    for i in range(len(Bs)):
        for j in range(i + 1, len(Bs)):
            for k in range(j + 1, len(Bs)):
                c = cross(sub(Bs[j], Bs[i]), sub(Bs[k], Bs[i]))
                if best is None or nrm(c) > nrm(best): best = c
    if best is None: return None
    F = unit(best); F = F if dot(sub(G, As), F) > 0 else [-x for x in F]
    return G, F, nrm(sub(As, G)) / math.sqrt(1.64)
for obj, rs in rows.items():
    gates = Counter(r['tgt'] for r in rs if r['tgt_class'] == 18)
    if not gates: continue
    g = gates.most_common(1)[0][0]; gf = gate_frame(rs, g)
    if not gf: print('== 0x%08x: no gate frame' % obj); continue
    G, F, rg = gf
    P = M(Counter(r['given'] for r in rs if r['order'] == 4 and r['tgt'] == g).most_common(1)[0][0])
    s_of = lambda r: dot(sub(M(r['pos']), G), F)
    def e_of(r):
        d = sub(M(r['pos']), P); return math.sqrt(max(0, dot(d, d) - dot(d, F) ** 2))
    js = joins.get(obj, []); ji = 0; mode = 'out'
    segs = []
    for r in rs:
        while ji < len(js) and js[ji][0] <= r['frame']: mode = js[ji][1]; ji += 1
        o = r['order']
        lab = {0x1f: 'rotate', 0x12: 'evade0x12', 5: 'launch5', 0: 'order0'}.get(o, 'o%x' % o)
        if o == 4: lab = 'o4:' + mode + ('+floor' if r['des'] <= 1 else '')
        if segs and segs[-1][0] == lab: segs[-1][1].append(r)
        else: segs.append([lab, [r]])
    first = next((r for r in rs if r['order'] == 4 and r['tgt'] == g), rs[0])
    print('== 0x%08x %s gate 0x%08x P lateral %.0f m from the axis, P depth %.0f m; rows %d frames %d-%d' % (
        obj, rs[0]['class'], g, math.sqrt(max(0, dot(sub(P, G), sub(P, G)) - dot(sub(P, G), F) ** 2)), dot(sub(P, G), F), len(rs), rs[0]['frame'], rs[-1]['frame']))
    print('   label frames | t_game_s dur_game_s real_s | s_m s/e | e_m s/e | hdg_err s/e | v_ms min..max | turned_deg net_disp_m')
    tot = Counter(); rf = lambda f: fe.get(f, fe.get(f - 1, 0)) / 1e3
    for lab, s in segs:
        a, b = s[0], s[-1]; dur = (b['t_ms'] - a['t_ms'] + b['dt_ms']) / 1e3; tot[lab] += dur
        turned = sum(ang(x['fwd'], y['fwd']) for x, y in zip(s, s[1:]))
        if dur < MIN_S and lab.startswith('o4:'): continue  # short order-4 flaps (floor/join at the D_x line) count in the totals only
        vs = [x['v'] / 500 for x in s]
        print('   %-18s %d-%d | %.1f %.1f %.1f | %.0f/%.0f | %.0f/%.0f | %.0f/%.0f | %.0f..%.0f | %.0f %.0f' % (
            lab, a['frame'], b['frame'], a['t_ms'] / 1e3, dur, rf(b['frame']) - rf(a['frame']), s_of(a), s_of(b), e_of(a), e_of(b),
            ang(a['fwd'], F), ang(b['fwd'], F), min(vs), max(vs), turned, nrm(sub(M(b['pos']), M(a['pos'])))))
        if lab.startswith('o4:') and dur >= MIN_S:
            L = [d for d in lanes.get(obj, []) if a['frame'] <= int(d['frame']) <= b['frame']]
            per = defaultdict(list)
            for d in L: per[d['B']].append(d)
            for B, ds in per.items():
                vr = Counter(d['verdict'] + '/' + d['route'] for d in ds); dsv = [float(d['ds_m']) for d in ds]
                sc = [float(d.get('seq_cap', d.get('v_cap', '-1'))) for d in ds if float(d.get('seq_cap', d.get('v_cap', '-1'))) >= 0]  # run160: seq_cap gone, v_cap=-1 for (s)
                print('      vs %s rows %d %s bind=1 %d ds %.0f..%.0f seq_cap rows %d min %s trail=A %d | A e_m med %.0f theta med %.1f' % (
                    B, len(ds), dict(vr), sum(d['bind'] == '1' for d in ds), min(dsv), max(dsv), len(sc), '%.1f' % min(sc) if sc else '-',
                    sum(d['trail'] == '0x%08x' % obj for d in ds), St.median(float(d['e_m']) for d in ds), St.median(float(d['theta_deg']) for d in ds)))
    print('   totals (game s):', {k: round(v, 1) for k, v in tot.items()})
