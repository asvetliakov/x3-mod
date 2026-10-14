# Run167 Q5: members with no progress for > 120 game s while not holding: per member, runs of ship_gate_ctl_member rows outside
# stage hold over which the position moved < 500 m in any 120 game s span. Usage: python3 -I stuck.py rows.txt
import sys, re, math, collections as C
ids = {}; mem = C.defaultdict(list)
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids.setdefault(d['ship'], d['id'])
    elif l.startswith('ship_gate_ctl_member '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); mem[d['ship']].append((int(d['t_ms']) / 1000, d['stage'], d['role'], [float(x) for x in d['p_m'].split(',')], float(d['v_ms'])))
n = 0
for s, v in mem.items():
    j = 0; hit = []
    for i in range(len(v)):
        while v[i][0] - v[j][0] > 120: j += 1
        seg = v[j:i + 1]
        if v[i][0] - v[j][0] >= 119 and all(x[1] != 'hold' for x in seg) and math.dist(v[i][3], v[j][3]) < 500 and all(x[2] == seg[0][2] for x in seg):
            hit.append((v[j][0], v[i][0], v[i][1], v[i][2]))
    if hit: n += 1; print(ids.get(s, s), 'stuck spans', len(hit), 'first %.1f-%.1f %s %s' % hit[0], 'last %.1f-%.1f %s %s' % hit[-1])
print('members with a stuck span:', n, 'of', len(mem))
