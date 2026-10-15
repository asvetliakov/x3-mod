# Run174 Q2: every ship_gate_ctl leave with why=target, the leaving ship's ship_trace row of the same frame (our avoidance resolve's
# obstacle obst=, leaf point vs the order field given=) and whether its previous trace row already had the obstacle; then, for every
# traced ship, the count of order-4 frames with a resolve obstacle while committed (frames between a commit and its leave).
# Usage: python3 -I target_leaves.py ROWS
import sys, re, collections as C
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
leaves = []; tr = C.defaultdict(dict); ctl = C.defaultdict(list)
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ctl '):
        d = kv(l); ctl[d['ship']].append((int(d['frame']), d['to'], d['why']))
        if d['why'] == 'target': leaves.append(d)
    elif l.startswith('ship_trace frame='):
        d = kv(l); tr[d['obj']][int(d['frame'])] = (d['order'], d['obst'], d['point'] != d['given'], d['tgt'])
n = C.Counter()
for d in leaves:
    s, f = d['ship'], int(d['frame']); t = tr[s].get(f); fs = sorted(x for x in tr[s] if x < f); tp = tr[s].get(fs[-1]) if fs else None
    k = 'untraced' if not t else ('obst' if t[1] != '0x00000000' else 'no_obst')
    n[k] += 1
    print('f=%d t=%s ship=%s gate=%s order=%s trace=%s prev=%s' % (f, d['t_ms'], s, d['gate'][-4:], d['order'], t, tp))
print('why=target leaves:', dict(n))
# committed order-4 frames with a resolve obstacle (traced ships), and how many ended the commitment within 2 frames
inside = C.Counter()
for s, ev in ctl.items():
    cur = None
    for f, to, why in ev + [(1 << 30, 'end', '-')]:
        if cur is not None:
            for g in sorted(x for x in tr[s] if cur <= x < f):
                o = tr[s][g]
                if o[0] == '0x4' and o[1] != '0x00000000': inside['committed_o4_obst_frames'] += 1; inside['ships:' + s] += 1
        cur = f if to == 'committed' else None
print(dict(inside))
