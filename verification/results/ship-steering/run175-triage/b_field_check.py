# Run175 B Q4: on every traced order-4 frame of a capital toward a lane gate, is the order's field (given=) still a KC grid point
# G - 2(r_gate + r_s)F + 666 (a R + b U) (not rewritten: |a|,|b| <= 2 integers, within 2 m), and does the leaf point (point=) differ from
# the field by > 50 m (our avoidance detour or a field the engine re-plans)? Per ship and gate: frames, grid-field frames (with game-time
# range), point != field frames. The gate frame from the lane summary (F; R and U from the frame's axes: F=(0,0,+-1) -> R=(1,0,0),U=(0,1,0);
# F=(-1,0,0) -> R=(0,0,1)... taken as any orthonormal pair, the 666 m test uses the lateral distance's decomposition in that pair).
# Usage: python3 -I b_field_check.py ROWS SHIP[,SHIP...]
import sys, re, math, collections as C
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
ships = set(sys.argv[2].split(',')); GF = {}; out = C.defaultdict(lambda: [0, 0, None, None, 0, set()])
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_steering_lane_summary ') and 'gates_gate:F:G:r=' in l:
        for e in l.split('gates_gate:F:G:r=')[1].split()[0].split('|'):
            g, f, gg, rr = e.split(':'); GF.setdefault(g, ([float(x) for x in f.split(',')], [float(x) for x in gg.split(',')], float(rr)))
    elif l.startswith('ship_trace frame=') and 'order=0x4 ' in l:
        d = kv(l)
        if d['obj'] not in ships or d['tgt'] not in GF: continue
        F, G, r = GF[d['tgt']]; A = [int(x) / 500 for x in d['given'].split(',')]; P = [int(x) / 500 for x in d['point'].split(',')]
        v = [A[i] - G[i] for i in range(3)]; s = sum(v[i] * F[i] for i in range(3)); lat = [v[i] - s * F[i] for i in range(3)]
        R = [1.0, 0.0, 0.0] if abs(F[0]) < 0.5 else [0.0, 0.0, 1.0]; U = [0.0, 1.0, 0.0]
        a = sum(lat[i] * R[i] for i in range(3)) / 666; b = sum(lat[i] * U[i] for i in range(3)) / 666
        grid = abs(a - round(a)) * 666 < 2 and abs(b - round(b)) * 666 < 2 and abs(round(a)) <= 2 and abs(round(b)) <= 2 and s < -2 * r
        o = out[(d['obj'], d['tgt'][-4:])]; o[0] += 1; t = int(d['t_ms']) / 1000
        if grid:
            o[1] += 1; o[2] = o[2] or t; o[3] = t; o[5].add('%.0f' % (-s / 2 - r))
        if math.dist(A, P) > 50: o[4] += 1
for k, o in sorted(out.items()):
    print(k, 'order-4 frames %d, field still a grid point %d %s implied size %s, point != field (>50 m) %d' % (
        o[0], o[1], '(%.1f-%.1f)' % (o[2], o[3]) if o[2] else '', sorted(o[5]), o[4]))
