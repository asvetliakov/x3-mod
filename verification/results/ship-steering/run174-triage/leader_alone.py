# Run174 coordinator follow-up: the group-2 leader 0x39e43950 alone after its arrival (launch 23848.1 .. escort 0x90448e28 launch
# 23929.7, then to 24010): every ~4 game s of ship_trace: order, distance and nose angle to the gate centre, gate-frame s / lateral, v,
# our desired speed (des) vs the engine's (des_engine), leaf point vs field (detour), our resolve obstacle, the yield cap; plus counts of
# every row kind naming it in the window and its lane law/write runs.
# Usage: python3 -I leader_alone.py ROWS [T_END_MS]
import sys, re, math, collections as C
rows = sys.argv[1]; T0, T1 = 23848086, int(sys.argv[2]) if len(sys.argv) > 2 else 23929726
G = (-7498, 10774, -57569); OBJ = '0x39e43950'
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
last = -1e9; kinds = C.Counter(); laws = []; ev = []
for l in open(rows, errors='replace'):
    if not l.startswith('ship_') or OBJ not in l: continue
    m = re.search(r' t_ms=(\d+)', l); d = kv(l)
    if m: t = int(m[1])
    f = int(d.get('frame', 0))
    if not (25826 <= f <= 27300): continue
    kinds[l.split()[0] + ('(A)' if d.get('A') == OBJ else '(B)' if d.get('B') == OBJ or OBJ in l.split('entries')[-1] and d.get('obj') != OBJ else '')] += 1
    if l.startswith('ship_gate_ctl_lane') and d['ship'] == OBJ:
        k = (d['law'], d['w'], d['v_law'] if d['law'] == 'stop' else '')
        if not laws or laws[-1][0] != k: laws.append([k, int(d['t_ms']), int(d['t_ms'])])
        else: laws[-1][2] = int(d['t_ms'])
    if l.startswith(('ship_gate_ctl ', 'ship_gate_ctl_rewrite', 'ship_avoid_engine_warn', 'ship_gate_launch')) and OBJ in l: ev.append(l.split()[0] + ' ' + ' '.join(x for x in l.split()[1:] if x.split('=')[0] in ('frame', 't_ms', 'from', 'to', 'why', 'site', 'from_m', 'to_m', 'other', 'order', 'kind')))
    if l.startswith('ship_trace frame=') and d['obj'] == OBJ and int(d['t_ms']) - last >= 4000 and int(d['t_ms']) <= T1 + 80000:
        last = int(d['t_ms']); p = [int(x) / 500 for x in d['pos'].split(',')]; fw = [float(x) for x in d['fwd'].split(',')]
        dv = [G[i] - p[i] for i in range(3)]; dd = math.hypot(*dv); ang = math.degrees(math.acos(max(-1, min(1, sum(dv[i] * fw[i] for i in range(3)) / dd))))
        print('g=%.1f ord=%s d_gate=%.0f nose_to_gate=%.0f deg s=%.0f lat=%.0f v=%.1f des=%.1f des_eng=%.1f detour=%d obst=%s cap=%s' % (
            last / 1000, d['order'], dd, ang, -(p[2] - G[2]) * -1, math.hypot(p[0] - G[0], p[1] - G[1]), int(d['v']) / 500, int(d['des']) / 500,
            int(d['des_engine']) / 500, d['point'] != d['given'], d['obst'][-4:], d['cap_ms']))
print('lane law/write runs (law, w, v_law if stop; from..to game s):', [(k, a / 1000, b / 1000) for k, a, b in laws])
print('events:'); [print('  ', e) for e in ev]
print('row kinds naming the leader, frames 25826-27300:', dict(kinds))
