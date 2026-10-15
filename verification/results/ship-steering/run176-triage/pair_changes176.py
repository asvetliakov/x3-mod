# Run176 Q2: ship_gate_ctl_pair rows at one gate, printed when (a, b, kind, verdict, before, why, yielder, cause, trig) changes: game t, seq, rank,
# t_c_s, cap_ms, d_m, gap_m.  Usage: python3 -I pair_changes176.py ROWS GATE
import sys, re
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l)); prev = None
for l in open(sys.argv[1], errors='replace'):
    if not l.startswith('ship_gate_ctl_pair ') or 'gate=' + sys.argv[2] not in l: continue
    d = kv(l); key = tuple(d[k] for k in ('a', 'b', 'kind', 'verdict', 'before', 'why', 'yielder', 'cause', 'trig'))
    if key != prev: print(d['frame'], '%.1f' % (int(d['t_ms']) / 1000), *['%s=%s' % (k, d[k]) for k in ('a', 'b', 'seq', 'rank', 'kind', 'verdict', 'before', 'why', 't_c_s', 'yielder', 'cap_ms', 'd_m', 'cause', 'trig', 'gap_m')])
    prev = key
