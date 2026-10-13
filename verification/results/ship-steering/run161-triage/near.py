"""Run160b questions 3/4: near-gate detours by pair class, engine warns with a capital subject or a capital other,
emg frames per traced capital, min capital-capital centre distance, small ships evading a capital that was on an (s)/(a) pair.
Gate frames from gates.txt; 'near' = obj position (detour x_m) within 6 r_gate of a gate centre. Committed capital = order
0x4/0x5/0x1f. Episode = consecutive detour rows of one (obj, obstacle) with frame gap <= 60. Game time from ship_trace t_ms.
Usage: python3 -I near.py rows.txt gates.txt caps.txt"""
import sys, re, math, bisect, collections as C
GT = {}
for l in open(sys.argv[2]):
    p = l.split()
    if len(p) > 3 and p[1] != 'insufficient': GT[p[0]] = ([float(x) for x in p[1].split(',')], [float(x) for x in p[2].split(',')], float(p[3]))
caps = {l.split()[0]: l.split()[1] for l in open(sys.argv[3])}
rB = {}; cls = dict(caps); tm = {}; det = []; warns = []; lanes = []; Ecc = []; emg = C.defaultdict(list); trpos = C.defaultdict(dict)
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        m = re.match(r'ship_trace frame=(\d+) t_ms=(\d+) obj=(\S+) class=(\S+) order=(\S+) tgt=\S+ tgt_class=\d+ pos=(\S+) .* v=(\d+) .* emg=(\d+)', l)
        f = int(m[1]); tm.setdefault(f, int(m[2])); cls.setdefault(m[3], m[4])
        if m[3] in caps:
            p = [int(x) / 500 for x in m[6].split(',')]; trpos[m[3]][f] = p
            if m[8] != '0': emg[m[3]].append((f, m[5]))
    elif l.startswith('ship_avoid_detour '):
        det.append(dict(re.findall(r'(\w+)=(\S+)', l)))
    elif l.startswith('ship_avoid_engine_warn '):
        warns.append(dict(re.findall(r'(\w+)=(\S+)', l)))
    elif l.startswith('ship_capital_lane '):
        lanes.append(dict(re.findall(r'(\w+)=(\S+)', l)))
    elif l.startswith('ship_avoid_e_verdict '):
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        if d['B_code'] not in ('-',): cls.setdefault(d['B'], d['B_code'])
        if d['A'] in caps and d['B'] in caps: Ecc.append(d)
        if d['B'] in caps: rB[d['B']] = float(d['r_B'])
fs = sorted(tm)
def g(f): i = min(bisect.bisect_left(fs, f), len(fs) - 1); return tm[fs[i]] / 1e3
def near(x):
    best = None
    for gid, (G, F, r) in GT.items():
        d = math.dist(x, G); s = sum((a - b) * c for a, b, c in zip(x, G, F))
        if d <= 6 * r and (best is None or d < best[1]): best = (gid, d, s)
    return best
CAPS = set(caps); com = lambda o: o in ('0x4', '0x5', '0x1f')
def pc(d):
    a, b = d['obj'] in CAPS, d['obstacle'] in CAPS
    if a and b: return 'cap-cap committed' if com(d['order']) and com(d['obstacle_order']) else 'cap-uncommitted cap'
    return {(True, False): 'cap-small', (False, True): 'small-cap', (False, False): 'small-small'}[(a, b)]
print('== Q4 detours (all) by pair class:', dict(C.Counter(pc(d) for d in det)))
nd = []
for d in det:
    x = [float(v) for v in d['x_m'].split(',')]; nb = near(x)
    if nb: nd.append((d, nb))
print('== Q4 detours within 6 r_gate of a gate: rows %d by (gate, pair class): %s' % (len(nd), dict(C.Counter((nb[0], pc(d)) for d, nb in nd))))
ep = C.defaultdict(list)
for d, nb in nd: ep[(d['obj'], d['obstacle'])].append((int(d['frame']), d, nb))
print('   episodes (obj class order -> obstacle class order | gate frames game_s dur_s rows | miss min/C | |x-G| min m | kinds)')
for k, rs in sorted(ep.items(), key=lambda kv: kv[1][0][0]):
    segs = [[rs[0]]]
    for r in rs[1:]:
        if r[0] - segs[-1][-1][0] <= 60: segs[-1].append(r)
        else: segs.append([r])
    for s in segs:
        d0 = s[0][1]; miss = min(float(r[1]['miss_m']) for r in s); Cm = s[0][1]['C_m']
        print('   %s %s %s -> %s %s %s | %s %d-%d %.1f %.1f %d | %.0f/%s | %.0f | %s %s' % (k[0], cls.get(k[0], '?'), d0['order'], k[1], cls.get(k[1], '?'), d0['obstacle_order'],
              s[0][2][0], s[0][0], s[-1][0], g(s[0][0]), g(s[-1][0]) - g(s[0][0]), len(s), miss, Cm, min(r[2][1] for r in s),
              pc(d0), dict(C.Counter(r[1]['kind'] for r in s))))
print('== Q4 engine warns with a capital subject or a capital other')
for w in warns:
    if w['obj'] in CAPS or w['other'] in CAPS:
        print('   %s %.1f %s %s order %s -> %s %s class %s d_m %s' % (w['frame'], g(int(w['frame'])), w['obj'], cls.get(w['obj'], '?'), w['order'], w['other'], cls.get(w['other'], w.get('code')), w['class'], w['d_m']))
print('== Q4 emg=1 trace frames per traced capital:', {o: (len(v), dict(C.Counter(x[1] for x in v)), (v[0][0], v[-1][0])) for o, v in emg.items()})
print('== Q4 min capital-capital centre distance from E rows (upper bound, 1 row/pair/s) and from two traced positions')
best = {}
for d in Ecc:
    k = tuple(sorted((d['A'], d['B']))); v = (float(d['d_m']), int(d['frame']), d['A_order'], d['B_order'], d['verdict'])
    if k not in best or v < best[k]: best[k] = v
R = {'M1': 1022, 'M2': 1092, 'M7': None}
cen = {}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_capital_census'):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); cen[d['class'] + ':' + d['type']] = float(d['r_m'])
rr = {'M1': 1022, 'M2': 1092}
for k, v in sorted(best.items(), key=lambda kv: kv[1][0]):
    rs = [rB.get(x) for x in k]
    print('   %s %s %s %s min d %.0f m frame %d game %.1f orders %s/%s verdict %s | rA+rB %s' % (k[0], cls[k[0]], k[1], cls[k[1]], v[0], v[1], g(v[1]), v[2], v[3], v[4], '%.0f' % sum(rs) if None not in rs else '-'))
obj = sorted(trpos); 
for i in range(len(obj)):
    for j in range(i + 1, len(obj)):
        A, B = trpos[obj[i]], trpos[obj[j]]; com_f = set(A) & set(B)
        if com_f:
            dm, f = min((math.dist(A[f], B[f]), f) for f in com_f); print('   traced pair %s %s min %.0f m frame %d' % (obj[i], obj[j], dm, f))
print('== Q4 small subjects warned against a capital, with that capital lane rows (s/a) within +-30 frames')
lf = C.defaultdict(list)
for d in lanes: lf[d['A']].append((int(d['frame']), d['verdict'], d['B'])); lf[d['B']].append((int(d['frame']), d['verdict'], d['A']))
for w in warns:
    if w['obj'] not in CAPS and w['other'] in CAPS:
        f = int(w['frame']); L = [x for x in lf.get(w['other'], []) if abs(x[0] - f) <= 30]
        print('   %s %s %s order %s vs %s %s d %s | capital lane rows near: %s' % (f, w['obj'], cls.get(w['obj'], '?'), w['order'], w['other'], cls[w['other']], w['d_m'], dict(C.Counter(x[1] for x in L)) or '-'))
sd = [(d, nb) for d, nb in nd if pc(d) == 'small-cap']
print('   small-cap detours near a gate whose obstacle capital had lane rows +-30 frames: %d of %d' % (sum(1 for d, nb in sd if any(abs(x[0] - int(d['frame'])) <= 30 for x in lf.get(d['obstacle'], []))), len(sd)))
