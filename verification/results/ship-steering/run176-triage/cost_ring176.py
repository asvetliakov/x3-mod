# Run176 cost triage Q2: the gate collision-tree reads. ship_gate_ring_node field tallies per gate; every ship_gate_ring_reread with the
# game ms since the previous re-read of the gate and since the gate's last ship_gate_launch (an arrival through it) and the count of
# launches at the gate in the 10 s game before; nodes 33 vs 1 against that. Usage: python3 -I cost_ring176.py ROWS
import sys, re, bisect, collections as C
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
nodes = C.defaultdict(C.Counter); rr = []; la = C.defaultdict(list); first = {}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_gate_ring_node '):
        d = kv(l); nodes[d['gate']][(d['depth'], d['scene'], d['f25c'], d['f140'], d['flags'])] += 1
    elif l.startswith('ship_gate_ring_reread '): rr.append(kv(l))
    elif l.startswith('ship_gate_ring_read '): d = kv(l); first[d['gate']] = d
    elif l.startswith('ship_gate_launch '): d = kv(l); la[d['gate']].append(int(d['t_ms']))
for g, c in nodes.items():
    print('ring_node gate', g, 'rows', sum(c.values()), ' '.join('depth=%s scene=%s f25c=%s f140=%s flags=%s x%d' % (k + (n,)) for k, n in c.items()))
for g, d in first.items(): print('first read', g, 'nodes', d['nodes'], 'state', d['state'], 'frame', d['frame'])
prev = {}; tab = C.Counter(); gaps = C.Counter()
for d in rr:
    g = d['gate']; t = int(d['t_ms']); L = la.get(g, []); i = bisect.bisect_right(L, t)
    since = t - L[i - 1] if i else -1; n10 = i - bisect.bisect_left(L, t - 10000)
    gap = t - prev[g] if g in prev else -1; prev[g] = t
    tab[g, d['nodes']] += 1
    print('reread frame=%s t_ms=%d gate=%s nodes %s->%s changed=%s state=%s gap_ms=%d since_launch_ms=%d launches_10s=%d'
          % (d['frame'], t, g, d['nodes_before'], d['nodes'], d['changed'], d['state'], gap, since, n10))
print('rereads per (gate, nodes):', dict(tab))
