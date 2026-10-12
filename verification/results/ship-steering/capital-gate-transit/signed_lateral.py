"""Run157: did the traced M2 leader 0x8c30aad0 cross the gate axis on its lead-in legs? (amendment 15 evidence)
Usage: LC_ALL=C grep -a '^ship_trace frame=.*obj=0x8c30aad0' /mnt/c/X3/x3m.log | python3 -I signed_lateral.py
Geometry from run157-triage/capital_timeline.txt: grid point P = (-7498, 10774, -53177) m, F = (0, 0, -1), r_gate 1105 m,
X = P - 0.75 r F. The lateral offset is the (x, y) part of pos - X (F is the -z axis); the axis is crossed when the
lateral vector's component along the segment's first lateral direction e_ref changes sign (the 2-D lateral vector rotates
past the axis at a small miss distance, so consecutive rows never point opposite).
Prints, per order segment that ends or contains a crossing, the crossing rows (frame, order, s/r, nose angle to F,
velocity-direction proxy = nose, speed) and the minimum |e| per lead segment. Units: 500 units = 1 m."""
import math, re, sys
P = (-7498.0, 10774.0, -53177.0); RG = 1105.0; X = (P[0], P[1], P[2] + 0.75 * RG)
pat = re.compile(r'ship_trace frame=(\d+) t_ms=(\d+) obj=(\S+) class=\S+ order=(\S+) tgt=(\S+) tgt_class=\d+ pos=(-?\d+),(-?\d+),(-?\d+) '
                 r'fwd=(\S+),(\S+),(\S+) .* v=(-?\d+) des=(-?\d+) des_engine=(-?\d+) point=(-?\d+),(-?\d+),(-?\d+) given=(-?\d+),(-?\d+),(-?\d+)')
rows = []
for l in sys.stdin:
    m = pat.match(l)
    if not m: continue
    g = m.groups()
    fr, order = int(g[0]), int(g[3], 16)
    pos = [int(g[5 + i]) / 500 for i in range(3)]; fwd = [float(g[8 + i]) for i in range(3)]
    v = int(g[11]) / 500; point = [int(g[14 + i]) / 500 for i in range(3)]; given = [int(g[17 + i]) / 500 for i in range(3)]
    lat = (pos[0] - X[0], pos[1] - X[1]); s = -(pos[2] - X[2])
    rows.append((fr, order, lat, s / RG, math.degrees(math.acos(max(-1, min(1, -fwd[2])))), v, point != given, fwd))
prev = None; ref = None; crossings = []
for r in rows:
    fr, order, lat, s_r, hdg, v, lead, fwd = r
    e = math.hypot(*lat)
    if prev is None or prev[1] != order:
        ref = (lat[0] / e, lat[1] / e) if e > 0 else (1.0, 0.0)
    elif (prev[2][0] * ref[0] + prev[2][1] * ref[1]) * (lat[0] * ref[0] + lat[1] * ref[1]) < 0:
        crossings.append((fr, order, s_r, hdg, v, lead, math.hypot(*prev[2]), e))
        ref = (-ref[0], -ref[1])
    prev = r
print('rows %d frames %d..%d' % (len(rows), rows[0][0], rows[-1][0]))
print('axis crossings (frame, order, s/r, nose angle to F deg, v m/s, lead-in active, |e| before/after m):')
for c in crossings:
    print('  %d 0x%x %.2f %.1f %.0f %s %.0f/%.0f' % c)
print('min |e| per order-4 lead segment (frame range, min e m, at frame, nose angle there, s/r):')
cur = None
for r in rows:
    k = r[1]
    if cur is None or cur[0] != k:
        if cur and cur[0] == 4 and cur[3]: print('  %d-%d %.0f @%d hdg %.1f s/r %.2f' % (cur[1], cur[2], cur[4][0], cur[4][1], cur[4][2], cur[4][3]))
        cur = [k, r[0], r[0], 0, (1e9, 0, 0, 0)]
    cur[2] = r[0]; cur[3] += r[6]
    e = math.hypot(*r[2])
    if e < cur[4][0]: cur[4] = (e, r[0], r[4], r[3])
if cur and cur[0] == 4 and cur[3]: print('  %d-%d %.0f @%d hdg %.1f s/r %.2f' % (cur[1], cur[2], cur[4][0], cur[4][1], cur[4][2], cur[4][3]))
