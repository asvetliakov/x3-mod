# Run164 Q1/Q2: every traced capital member's petal laps per membership stage interval (ship_gate_ctl rows: role/stage), not only on order 4.
# A lap = the nose turning against the gate's F (nose.F < -0.9) and back along F (> 0.9). Per interval: role, stage, game/real span, traced share,
# order set, laps (count, per-lap game/real s), the accumulated turn (deg), distance to G range (m). Gates from gates.txt (G, F).
# Usage: python3 -I sb_laps_all.py rows.txt
import sys, os, re, math, collections as C; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sb_time
D = os.path.dirname(os.path.abspath(__file__)); tm = sb_time.load(sys.argv[1])
GT = {}
for l in open(os.path.join(D, 'gates.txt')):
    p = l.split()
    if len(p) > 3: GT[p[0]] = ([float(x) for x in p[1].split(',')], [float(x) for x in p[2].split(',')])
dot = lambda x, y: sum(a * b for a, b in zip(x, y))
ev = C.defaultdict(list); ids = {}; T = C.defaultdict(list)
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids[d['ship']] = d['id']
        ev[d['ship']].append((int(d['frame']), d['gate'], d['to'], d['stage'].split('>')[1], d['why']))
    elif l.startswith('ship_trace frame='):
        m = re.match(r'ship_trace frame=(\d+) t_ms=(\d+) obj=(\S+) class=\S+ order=(\S+) tgt=(\S+)', l)
        if not m: continue
        if m[3] in ids or m[3] in ev:
            d = dict(re.findall(r'(\w+)=(\S+)', l))
            T[m[3]].append((int(m[1]), int(m[2]), m[4], [float(x) / 500 for x in d['pos'].split(',')], [x / (math.sqrt(sum(y * y for y in map(float, d['fwd'].split(',')))) or 1.0) for x in map(float, d['fwd'].split(','))], float(d['v']) / 500))  # Run164: normalized fwd
end_f = max(tm.rf)
for s in sorted(ev, key=lambda s: ev[s][0][0]):
    rows = ev[s]; print('== %s id %s' % (s, ids[s]))
    for i, (f, gate, role, stage, why) in enumerate(rows):
        f1 = rows[i + 1][0] if i + 1 < len(rows) else end_f
        if f1 <= f: continue
        G, F = GT.get(gate, (None, None)); seg = [x for x in T[s] if f <= x[0] < f1]
        g0, g1 = tm.game(f) / 1000, tm.game(f1) / 1000; r0, r1 = tm.real(f) / 1000, tm.real(f1) / 1000
        if not seg or not G:
            print('  f %6d-%6d %-9s %-8s game %8.1f +%6.1f real %5.1f +%5.1f | untraced' % (f, f1, role, stage, g0, g1 - g0, r0, r1 - r0)); continue
        st = None; laps = []; back = None; turn = 0.0; pf = None; ds = []; ords = C.Counter()
        for (fr, t, od, p, fw, v) in seg:
            nf = dot(fw, F); dd = [p[k] - G[k] for k in range(3)]; ds.append(math.sqrt(dot(dd, dd))); ords[od] += 1
            if pf: turn += math.degrees(math.acos(max(-1, min(1, dot(pf, fw)))))
            pf = fw
            if st is None: st = 'fwd' if nf > 0 else 'back'
            if st == 'fwd' and nf < -0.9: st = 'back'; back = fr if back is None else back; laps.append([fr, None])
            elif st == 'back' and nf > 0.9 and laps: st = 'fwd'; laps[-1][1] = fr
        cov = (seg[-1][1] - seg[0][1]) / 1000 / max(0.001, g1 - g0)
        lp = ' '.join('%.0f/%.1f' % ((tm.game(b or f1) - tm.game(a)) / 1000, (tm.real(b or f1) - tm.real(a)) / 1000) for a, b in laps)
        print('  f %6d-%6d %-9s %-8s game %8.1f +%6.1f real %5.1f +%5.1f | traced %.2f orders %s | turn %5.0f deg, against-F episodes %d [%s] | d_G %.0f..%.0f' % (
            f, f1, role, stage, g0, g1 - g0, r0, r1 - r0, cov, dict(ords), turn, len(laps), lp, min(ds), max(ds)))
