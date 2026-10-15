# Run176 cost triage Q2: each ship_gate_ring_reread's node count against the player's distance to that gate (the nearest
# ship_player_pos row in game time; gate positions = the mean ship_gate_launch p_m at the gate, within ~1 km of the gate centre).
# Usage: python3 -I cost_ring_player176.py ROWS
import sys, re, bisect, collections as C
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
pp = []; gp = C.defaultdict(list); rr = []
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_player_pos '): d = kv(l); pp.append((int(d['t_ms']), [float(v) for v in d['pos'].split(',')]))
    elif l.startswith('ship_gate_launch '): d = kv(l); gp[d['gate']].append([float(v) for v in d['p_m'].split(',')])
    elif l.startswith('ship_gate_ring_reread '): rr.append(kv(l))
G = {g: [sum(p[i] for p in v) / len(v) for i in range(3)] for g, v in gp.items()}
pp.sort(); T = [t for t, _ in pp]
by = C.defaultdict(list)
for d in rr:
    t = int(d['t_ms']); i = bisect.bisect_left(T, t); c = [j for j in (i - 1, i) if 0 <= j < len(pp)]
    j = min(c, key=lambda j: abs(T[j] - t)); g = G.get(d['gate'])
    if not g: continue
    dist = sum((a - b) ** 2 for a, b in zip(pp[j][1], g)) ** .5
    by[d['nodes']].append((round(dist / 1000, 1), abs(T[j] - t) // 1000))
    print('reread frame=%s gate=%s nodes=%s player_gate_km=%.1f pos_age_s=%d' % (d['frame'], d['gate'], d['nodes'], dist / 1000, abs(T[j] - t) // 1000))
for n, v in by.items():
    ds = sorted(x[0] for x in v); print('nodes=%s n=%d player-gate km min %.1f median %.1f max %.1f' % (n, len(v), ds[0], ds[len(ds) // 2], ds[-1]))
