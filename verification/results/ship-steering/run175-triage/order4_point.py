# Run175 Q2: a capital's order-4 leg against its order point A (the field the KC wrote, given=) and the engine's arrival test
# dist(ship, A) <= 3 r_gate / 4 (ship-navigation-ai.md 10.3): per traced frame on order 4 to GATE, d_A (m), s along the gate's F (m),
# and the leaf point (point=) vs given; printed once per EVERY game s and at each local minimum of d_A, with the pass minima at the end.
# Gate frame from the lane summary's gates_gate:F:G:r.  Usage: python3 -I order4_point.py ROWS SHIP GATE [EVERY_S]
import sys, re, math
rows, ship, gate = sys.argv[1:4]; every = float(sys.argv[4]) if len(sys.argv) > 4 else 5
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
F = G = r = None; tr = []
for l in open(rows, errors='replace'):
    if l.startswith('ship_steering_lane_summary ') and F is None and 'gates_gate:F:G:r=' in l:
        for e in l.split('gates_gate:F:G:r=')[1].split()[0].split('|'):
            g, f, gg, rr = e.split(':')
            if g == gate: F = [float(x) for x in f.split(',')]; G = [float(x) for x in gg.split(',')]; r = float(rr)
    elif l.startswith('ship_trace frame=') and 'obj=' + ship + ' ' in l:
        d = kv(l)
        if d['order'] == '0x4' and d['tgt'] == gate: tr.append(d)
w = lambda s: [int(x) / 500 for x in s.split(',')]
sub = lambda a, b: [a[i] - b[i] for i in range(3)]
dot = lambda a, b: sum(a[i] * b[i] for i in range(3))
print('gate', gate, 'F', F, 'G', G, 'r_gate', r, '3r/4', 0.75 * r)
lt = -1e9; ser = []
for d in tr:
    t = int(d['t_ms']) / 1000; p = w(d['pos']); A = w(d['given']); P = w(d['point'])
    dA = math.dist(p, A); s = dot(sub(p, G), F); sA = dot(sub(A, G), F); sP = dot(sub(P, G), F)
    eA = math.sqrt(max(0, dot(sub(A, G), sub(A, G)) - sA * sA)); eP = math.sqrt(max(0, dot(sub(P, G), sub(P, G)) - sP * sP))
    ser.append((t, dA, s))
    if t - lt >= every:
        print('f=%s g=%.1f d_A=%.0f s=%.0f v=%.1f fwd.F=%.2f A:s=%.0f e=%.0f point:s=%.0f e=%.0f point!=given=%d obst=%s' % (
            d['frame'], t, dA, s, int(d['v']) / 500, dot([float(x) for x in d['fwd'].split(',')], F), sA, eA, sP, eP, d['point'] != d['given'], d['obst']))
        lt = t
# the local minima of d_A (traced frames) below 2 r_gate, and the plane crossings (s from < 0 to >= 0) on order 4
for i in range(1, len(ser) - 1):
    if ser[i][1] <= ser[i - 1][1] and ser[i][1] < ser[i + 1][1] and ser[i][1] < 2 * r:
        print('d_A local minimum g=%.1f d_A=%.0f s=%.0f (arrival needs <= %.0f)' % (ser[i][0], ser[i][1], ser[i][2], 0.75 * r))
    if ser[i - 1][2] < 0 <= ser[i][2]: print('plane crossing on order 4 g=%.1f (no jump: order 4 has no transfer)' % ser[i][0])
print('last order-4 frame g=%.1f d_A=%.0f' % (ser[-1][0], ser[-1][1]))
