# Run167 Q6: ship_avoid_detour rows of controller members by the member's role/stage at that frame (last ship_gate_ctl_member row
# at or before it), with the obstacle (id/class if a member), vertical flag and min miss; funnel/handover rows are listed per episode
# (the backlog item 12 shape: a released member's final-straight detour). Usage: python3 -I member_detours.py rows.txt
import sys, re, collections as C, bisect
ids = {}; mem = C.defaultdict(list); det = []
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); ids.setdefault(d['ship'], d['id'])
    elif l.startswith('ship_gate_ctl_member '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); mem[d['ship']].append((int(d['frame']), d['role'], d['stage'], int(d['t_ms'])))
    elif l.startswith('ship_avoid_detour '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); det.append(d)
K = {s: [x[0] for x in v] for s, v in mem.items()}; agg = C.Counter(); ep = C.defaultdict(list)
for d in det:
    s = d['obj']
    if s not in mem: continue
    f = int(d['frame']); i = bisect.bisect_right(K[s], f) - 1
    if i < 0 or f - K[s][i] > 200: continue
    _, role, st, t = mem[s][i]; agg[(ids.get(s), role, st)] += 1
    if st in ('funnel', 'handover'): ep[(ids.get(s), ids.get(d['obstacle'], d['obstacle']), d['vertical'])].append((f, t / 1000, float(d['miss_m'])))
print('== member detour rows by (id, role, stage)')
for k, n in sorted(agg.items()): print(k, n)
print('== funnel/handover detours: (member, obstacle, vertical): rows, frames, game s, min miss m')
for k, v in ep.items(): print(k, len(v), v[0][0], '-', v[-1][0], '%.1f-%.1f' % (v[0][1], v[-1][1]), '%.0f' % min(x[2] for x in v))
