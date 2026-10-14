# Run165/166 Q1: game seconds with >= N live (st_age <= 5 s) M1/M2 members in stage hold or funnel per gate, from the
# ship_gate_ctl_member rows. Usage: python3 -I hold_funnel.py rows.txt (output appended to members.txt)
import sys, re, collections as C
S = C.defaultdict(lambda: C.defaultdict(set))
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl_member '):
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        if d['stage'] in ('hold', 'funnel') and 0 <= int(d['st_age_ms']) <= 5000 and d['class'] in ('M1', 'M2'): S[d['gate'][-4:]][int(d['t_ms']) // 1000].add(d['ship'])
for g in sorted(S):
  for n in (2, 3, 4, 5):
    k = sorted(s for s, v in S[g].items() if len(v) >= n)
    print('gate %s hold+funnel' % g + ' >= %d capitals: %d game s%s' % (n, len(k), ' (game %d..%d)' % (k[0], k[-1]) if k else ''))
