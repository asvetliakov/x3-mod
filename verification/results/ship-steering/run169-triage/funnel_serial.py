# Run169 Q4/Q5: per gate, each committed member's release -> X (handover) -> launch (ship_gate_ctl rows), its distance to the gate at the
# release (last member row |p_m|), and the overlap of consecutive members' [release, launch] and [X, launch] intervals (is anything ever
# two-deep in the funnel or in order 5?). Usage: python3 -I funnel_serial.py rows.txt
import sys, re, bisect, collections as C
ids = {}; ev = C.defaultdict(list); mem = C.defaultdict(list)
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids.setdefault(d['ship'], d['id']); ev[d['ship']].append((int(d['t_ms']), d['gate'], d['why']))
    elif l.startswith('ship_gate_ctl_member '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); mem[d['ship']].append((int(d['t_ms']), sum(float(x) ** 2 for x in d['p_m'].split(',')) ** 0.5))
T = C.defaultdict(list)
for s, v in ev.items():
    rel = None
    for t, g, w in v:
        if w == 'release': rel = (t, g)
        if w == 'handover' and rel: x = t
        if w == 'launch' and rel:
            ms = [m for m in mem[s] if m[0] <= rel[0]]; T[g].append((rel[0], x, t, ids[s], ms[-1][1] if ms else float('nan'))); rel = None
print('gate id | release game s | d_G at release km | release->X s | X->launch s | overlap with previous [release,launch] s | previous launch -> this release s')
for g, v in T.items():
    v.sort(); p = None
    for r, x, la, i, d in v:
        print(g[-4:], i, '| %.1f | %.1f | %.1f | %.1f | %s | %s' % (r / 1000, d / 1000, (x - r) / 1000, (la - x) / 1000, '%.1f' % ((p[2] - r) / 1000) if p and p[2] > r else '0', '%.1f' % ((r - p[2]) / 1000) if p else '-'))
        p = (r, x, la)
