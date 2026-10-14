# Run169 Q2 (B.9 oracle): ship_gate_ctl_pair rows: counts by kind/verdict/cause/trig, gap_m min per pair, and every row whose verdict
# differs from the pair's previous row (or whose cause changes), with cause trig gap_m d_m yielder cap_ms why.
# Usage: python3 -I pair_flips.py rows.txt
import sys, re, collections as C
ids = {}; rows = []
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids.setdefault(d['ship'], d['id'])
    elif l.startswith('ship_gate_ctl_pair '):
        rows.append(dict(re.findall(r'(\w+)=(\S+)', l)))
I = lambda s: ids.get(s, s)
cnt = C.Counter((r['kind'], r['verdict'], r['cause'], r['trig']) for r in rows)
print('rows', len(rows)); [print('  ', k, n) for k, n in cnt.most_common()]
prev = {}; gmin = C.defaultdict(lambda: 1e9); nrow = C.Counter()
print('== verdict/cause changes: game s gate a b | verdict before->now | cause trig gap_m d_m yielder cap_ms why kind')
for r in rows:
    k = (r['gate'], r['a'], r['b']); nrow[k] += 1
    if r['gap_m'] != 'na': gmin[k] = min(gmin[k], float(r['gap_m']))
    p = prev.get(k)
    if p is None or p['verdict'] != r['verdict'] or p['cause'] != r['cause']:
        print('%.1f %s %s %s | %s->%s | %s %s %s %s %s %s %s %s' % (int(r['t_ms']) / 1000, r['gate'][-4:], I(r['a']), I(r['b']), p['verdict'] if p else '-', r['verdict'],
              r['cause'], r['trig'], r['gap_m'], r['d_m'], I(r['yielder']), r['cap_ms'], r['why'], r['kind']))
    prev[k] = r
print('== per pair: rows, gap_m min')
for k in nrow: print(k[0][-4:], I(k[1]), I(k[2]), nrow[k], gmin[k] if gmin[k] < 1e9 else 'na')
