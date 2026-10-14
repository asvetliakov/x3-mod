# Run166 Q1/Q5: per escort (ship_gate_ctl_member role=escort; leader = its target ship) the leader's launch (order 8) and the escort's
# script order 4 (why=commit), release, X (handover), launch, as game/real seconds after the leader's launch; then per gate every launch
# -> the next release. Usage: python3 -I escorts.py rows.txt
import sys, os, re, collections as C; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import sb_time
tm = sb_time.load(sys.argv[1]); ids = {}; ev = C.defaultdict(list); lead = {}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids.setdefault(d['ship'], d['id']); ev[d['ship']].append((int(d['t_ms']), int(d['frame']), d['why'], d['gate']))
    elif l.startswith('ship_gate_ctl_member ') and ' role=escort ' in l:
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        if d['target'] != d['gate'] and d['target'] != '0x00000000': lead.setdefault(d['ship'], d['target'])
first = lambda s, w, after=0: next(((t, f) for t, f, x, g in ev[s] if x == w and t >= after), None)
print('escort leader | leader launch game/real | escort commit (order 4) +game/+real | release | X | launch (all after leader launch)')
for e, L in sorted(lead.items(), key=lambda kv: ev[kv[0]][0][0]):
    ll = first(L, 'launch')
    if not ll: print(ids[e], ids.get(L, L), 'leader not launched'); continue
    out = []
    for w in ('commit', 'release', 'handover', 'launch'):
        x = first(e, w, ll[0])
        out.append('%+.1f/%+.1f' % ((x[0] - ll[0]) / 1000, (tm.real(x[1]) - tm.real(ll[1])) / 1000) if x else '-')
    print('%s %s | %.1f/%.1f | %s' % (ids[e], ids.get(L, L), ll[0] / 1000, tm.real(ll[1]) / 1000, ' | '.join(out)))
