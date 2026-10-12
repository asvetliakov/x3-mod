"""Run159 triage, question 4: capital-vs-capital detours, E verdicts and engine warns.
Usage: python3 -I capcap.py <extracted rows>
Capitals: ship_trace class, ship_avoid_e_verdict B_code, ship_gate_approach capital=1 and warn codes in M1/M2/TL/M7/M0.
Each capital-capital ship_avoid_detour row is matched to the last ship_capital_lane row of the pair (either direction, any
earlier frame, and within 2 s game ~ <= 300 frames for 'recent'); detours against a pair whose last lane row is a or s are
the 16b invariant's flight check (target 0)."""
import re, sys
from collections import Counter, defaultdict
KV = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
CAP = {'M1', 'M2', 'TL', 'M7', 'M0'}
caps = {}; det = []; ev = []; lane = defaultdict(list); warn = []; pre = []
with open(sys.argv[1], errors='replace') as f:
    for l in f:
        if l.startswith('ship_trace frame='):
            m = re.search(r' obj=(\S+) class=(\S+) ', l)
            if m.group(2) in CAP: caps[m.group(1)] = m.group(2)
        elif l.startswith('ship_avoid_e_verdict '):
            d = KV(l); ev.append(d)
            if d['B_code'] in CAP: caps[d['B']] = d['B_code']
        elif l.startswith('ship_gate_approach ') and ' capital=1 ' in l:
            d = KV(l); caps[d['obj']] = d['class']
        elif l.startswith('ship_avoid_detour '): det.append(KV(l))
        elif l.startswith('ship_capital_lane '):
            d = KV(l); lane[frozenset((d['A'], d['B']))].append((int(d['frame']), d['verdict'], d['route']))
        elif l.startswith('ship_avoid_engine_warn '): warn.append(KV(l))
print('capitals seen:', len(caps), dict(Counter(caps.values())))
cc = [d for d in det if d['obj'] in caps and d['obstacle'] in caps]
print('capital-vs-capital detour rows', len(cc), 'of', len(det), '| by obj order', dict(Counter(d['order'] for d in cc)), 'obstacle order', dict(Counter(d['obstacle_order'] for d in cc)))
res = Counter(); ex = defaultdict(list)
for d in cc:
    fr = int(d['frame']); L = [x for x in lane.get(frozenset((d['obj'], d['obstacle'])), []) if x[0] <= fr]
    if not L: k = 'no lane row (untraced pair)'
    else:
        lf, v, r = L[-1]; k = '%s/%s %s' % (v, r, 'recent' if fr - lf <= 300 else 'stale>300f')
    res[k] += 1; ex[k].append((fr, d['obj'], d['order'], d['obstacle'], d['obstacle_order']))
for k, n in res.most_common():
    print('  last lane verdict %-24s %d  first %s  (obj order, obstacle order) %s' % (k, n, ex[k][0], dict(Counter((x[2], x[4]) for x in ex[k]))))
pairs = Counter((d['obj'], d['obstacle'], d['order'], d['obstacle_order']) for d in cc)
print(' pairs (obj, obstacle, obj order, obstacle order) x rows:')
for k, n in pairs.most_common(): print('   ', k, n, 'frames %d-%d' % (min(int(d['frame']) for d in cc if (d['obj'], d['obstacle'], d['order'], d['obstacle_order']) == k), max(int(d['frame']) for d in cc if (d['obj'], d['obstacle'], d['order'], d['obstacle_order']) == k)))
ce = [d for d in ev if d['A'] in caps and d['B'] in caps]
print('E verdict rows capital A vs capital B', len(ce), dict(Counter(d['verdict'] for d in ce)))
print('  by (A_order, B_order, verdict)', Counter((d['A_order'], d['B_order'], d['verdict']) for d in ce).most_common(12))
print('engine warn rows with a capital subject', [(w['frame'], w['obj'], w['other'], w['code'], w['d_m']) for w in warn if w['obj'] in caps])
# 0x12 episodes per capital, from every row that names its order: its own ship_trace rows (traced), detour rows (obj order,
# obstacle_order), E rows (A_order, B_order). An episode = consecutive 0x12 observations with no other order observed between;
# span = first..last 0x12 observation (a lower bound for an untraced ship), game seconds from the ship_trace t_ms of the frame.
import bisect
obs = defaultdict(list); tms = {}
with open(sys.argv[1], errors='replace') as f:
    for l in f:
        if l.startswith('ship_trace frame='):
            m = re.match(r'ship_trace frame=(\d+) t_ms=(\d+) obj=(\S+) class=\S+ order=(\S+) ', l)
            tms.setdefault(int(m.group(1)), int(m.group(2)))
            if m.group(3) in caps: obs[m.group(3)].append((int(m.group(1)), m.group(4)))
for d in det:
    if d['obj'] in caps: obs[d['obj']].append((int(d['frame']), d['order']))
    if d['obstacle'] in caps: obs[d['obstacle']].append((int(d['frame']), d['obstacle_order']))
for d in ev:
    if d['A'] in caps: obs[d['A']].append((int(d['frame']), d['A_order']))
    if d['B'] in caps: obs[d['B']].append((int(d['frame']), d['B_order']))
K = sorted(tms)
tg = lambda fr: tms[K[min(bisect.bisect_left(K, fr), len(K) - 1)]] / 1e3
print('0x12 episodes per capital (frames first-last 0x12 observation, game s span; traced ships exact, untraced lower bounds):')
for c in sorted(obs):
    o = sorted(set(obs[c])); eps = []; cur = None
    for fr, od in o:
        if od == '0x12':
            if cur is None: cur = [fr, fr]
            else: cur[1] = fr
        elif cur and fr > cur[1]: eps.append(cur); cur = None
    if cur: eps.append(cur)
    if eps: print('  %s %s n=%d total %.1f s: %s' % (c, caps[c], len(eps), sum(tg(b) - tg(a) for a, b in eps), ' '.join('%d-%d(%.1f)' % (a, b, tg(b) - tg(a)) for a, b in eps)))
# the E rows of the subject in the 60 frames before each 0x12 episode of a capital (frames >= 64000): which pair E passed
print('E rows of the capital as A in the 60 frames before each 0x12 onset (frame >= 64000): (B, B code, B_order, verdict, d_m) x n')
for c in sorted(obs):
    o = sorted(set(obs[c])); onsets = [fr for (fr, od), (pf, po) in zip(o[1:], o) if od == '0x12' and po != '0x12' and fr >= 64000]
    for on in onsets:
        E = [d for d in ev if d['A'] == c and on - 60 <= int(d['frame']) <= on]
        print('  %s onset %d: %s' % (c, on, dict(Counter((d['B'], d['B_code'], d['B_order'], d['verdict'], d['d_m']) for d in E).most_common(4))))
