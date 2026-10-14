# Run167 Q5 (B.6 yields_to): ship_gate_ctl_pair rows grouped by (gate, pair, kind, verdict, why, yielder) with count and game span,
# and the pairs whose both members were committed in funnel/handover/dock at once (from ship_gate_ctl_member rows, same game second).
# Usage: python3 -I pairs.py rows.txt
import sys, re, collections as C
ids = {}; G = C.OrderedDict(); occ = C.defaultdict(set); occ2 = C.defaultdict(set)
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids.setdefault(d['ship'], d['id'])
    elif l.startswith('ship_gate_ctl_pair '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); k = (d['gate'][-4:], d['a'], d['b'], d['kind'], d['verdict'], d['why'], d['yielder'], d['before'])
        t = int(d['t_ms']) / 1000; g = G.setdefault(k, [0, t, t, 9e9]); g[0] += 1; g[2] = t; g[3] = min(g[3], float(d['d_m']))
    elif l.startswith('ship_gate_ctl_member ') and 'role=committed' in l and re.search(r'stage=(funnel|handover|dock) ', l):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); occ[(d['gate'][-4:], int(d['t_ms']) // 1000)].add(d['ship']); occ2[(d['gate'][-4:], int(d['t_ms']) // 1000)].add((d['ship'], d['stage']))
I = lambda s: ids.get(s, s)
print('gate a b kind verdict why yielder before | rows game first..last | d_m min')
for k, (n, a, b, dm) in G.items(): print('%s %s %s %s %s %s %s %s | %d %.1f..%.1f | %.0f' % (k[0], I(k[1]), I(k[2]), k[3], k[4], k[5], I(k[6]), I(k[7]), n, a, b, dm))
both = C.defaultdict(list)
for (g, t), s in occ.items():
    if len(s) > 1: both[(g, tuple(sorted(I(x) for x in s)))].append(t)
print('== committed pairs in funnel/handover/dock at once (game s)')
for k, v in both.items(): print(k, len(v), 's', min(v), '..', max(v))
print('== both in stage funnel at once (the B.6 yields_to release-order clause applies only there)')
bf = C.defaultdict(list)
for (g, t), s in occ2.items():
    f = sorted(I(x) for x, st in s if st == 'funnel')
    if len(f) > 1: bf[(g, tuple(f))].append(t)
for k, v in bf.items(): print(k, len(v), 's', min(v), '..', max(v))
print('pairs', len(bf))
