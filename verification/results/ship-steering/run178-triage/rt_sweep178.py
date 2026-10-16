# Run178 Q2/Q3: for each 0x12 episode among the four return-trip ships (first ship_avoid_engine_warn row of an episode), reconstruct
# both hulls at the warn's game time from their last ship_trace rows at or before it (pos and fwd; velocity from the position change
# over the previous ~1 s of that ship's rows; extrapolated linearly to the warn time), check the reconstruction against the warn row's
# d_m (centre distance), and run the board's capsule sweep at the warn moment: capsules c +- (L - w) f, radius w (census l_m / w_m:
# M2 1092/583, M1 1022/546), 61 samples at 0.5 s over 30 s, margin 50 m + 10 m/s x t, noses held, constant velocities
# (gc::capsule_gap / gap_scan without the rollout). Then the flown path: the capsule gap of both traced hulls over the next 30 s
# (frames where both are traced within 0.3 s of each other), and over the whole return trip the closest pair approach.
# Usage: python3 -I rt_sweep178.py ROWS
import sys, re, bisect, math
DEC = 9.6  # m/s^2: 0x8fe612b8's order-0 deceleration after its arrival, 110.9 -> 41.5 m/s in 7.2 s (ship_gate_ctl_member v_ms)
S = {'0x8fe612b8': (1092, 583), '0x8fe58420': (1092, 583), '0x39cfa520': (1092, 583), '0x39cf2180': (1022, 546)}
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
tr = {s: [] for s in S}; pre = {}; warns = []; f2t = {}; ev_min = {}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        d = kv(l); f = int(d['frame']); t = int(d['t_ms']) / 1e3; f2t.setdefault(f, t)
        if d['obj'] in tr:
            tr[d['obj']].append((t, f, [int(x) / 500.0 for x in d['pos'].split(',')], [float(x) for x in d['fwd'].split(',')], int(d['v']) / 500.0, d['order']))
    elif l.startswith('ship_avoid_engine_warn '):
        d = kv(l)
        if d['obj'] in S and d['other'] in S: warns.append(d)
    elif l.startswith('ship_avoid_engine_warn_pre '):
        d = kv(l)
        if d['obj'] in S:
            e = l.split('e_verdict=')[1].split()[0].split(':'); pre[(d['obj'], int(d['frame']))] = (d['order'], e[3])
    elif l.startswith('ship_avoid_e_verdict '):
        d = kv(l)
        if d['A'] in S and d['B'] in S:
            k = tuple(sorted((d['A'], d['B']))); dm = float(d['d_m'])
            if dm < ev_min.get(k, (1e9,))[0]: ev_min[k] = (dm, int(d['frame']))
sub = lambda a, b: [a[i] - b[i] for i in range(3)]
add = lambda a, b, k=1.0: [a[i] + k * b[i] for i in range(3)]
dot = lambda a, b: sum(a[i] * b[i] for i in range(3))
def seg_seg(p1, q1, p2, q2):
    d1, d2, r = sub(q1, p1), sub(q2, p2), sub(p1, p2); a, e, f = dot(d1, d1), dot(d2, d2), dot(d2, r)
    c = dot(d1, r); b = dot(d1, d2); den = a * e - b * b
    s = min(1, max(0, (b * f - c * e) / den)) if den > 1e-9 else 0.0
    t = (b * s + f) / e if e > 1e-9 else 0.0
    if t < 0: t = 0; s = min(1, max(0, -c / a)) if a > 1e-9 else 0
    elif t > 1: t = 1; s = min(1, max(0, (b - c) / a)) if a > 1e-9 else 0
    x = sub(add(p1, d1, s), add(p2, d2, t)); return math.sqrt(dot(x, x))
def cap_gap(ca, fa, A, cb, fb, B):
    ha, hb = max(0, A[0] - A[1]), max(0, B[0] - B[1])
    return seg_seg(add(ca, fa, -ha), add(ca, fa, ha), add(cb, fb, -hb), add(cb, fb, hb)) - A[1] - B[1]
def state(s, t):  # last row at or before t; velocity over the previous ~1 s
    rows = tr[s]; ts = [r[0] for r in rows]; i = bisect.bisect_right(ts, t + 1e-6) - 1
    if i < 0: return None
    r = rows[i]; j = i
    while j > 0 and r[0] - rows[j - 1][0] <= 1.2: j -= 1
    if j == i: return None
    p0 = rows[j]; dt = r[0] - p0[0]
    v = [(r[2][k] - p0[2][k]) / dt for k in range(3)] if dt > 0.05 else [0, 0, 0]
    return dict(t=r[0], age=t - r[0], c=add(r[2], v, t - r[0]), f=r[3], v=v, spd=math.sqrt(dot(v, v)), order=r[5], dtv=dt)
def at(s, t):  # interpolated position and nose of a traced ship at t (rows within 0.3 s)
    rows = tr[s]; ts = [r[0] for r in rows]; i = bisect.bisect_left(ts, t)
    c = [rows[j] for j in (i - 1, i) if 0 <= j < len(rows) and abs(rows[j][0] - t) <= 0.3]
    if not c: return None
    r = min(c, key=lambda r: abs(r[0] - t)); return r[2], r[3]
last = {}
print('episode | A order/speed, B order/speed (state age s) | centre d: reconstructed vs warn d_m | sweep at the warn: min gap - margin (m) at t | flown 30 s: min capsule gap (m) at t, frames compared | verdict')
for d in warns:
    s, f, o = d['obj'], int(d['frame']), d['other']
    if last.get(s, -10) >= f - 3: last[s] = f; continue
    last[s] = f
    t = f2t.get(f)
    a, b = state(s, t), state(o, t)
    if not a or not b: print('%.1f %s vs %s: no state' % (t, s, o)); continue
    dc = math.sqrt(dot(sub(a['c'], b['c']), sub(a['c'], b['c'])))
    # the orders before the warn: A's last traced order before 0x12 at or before the warn frame (else the warn_pre row), B's last traced order
    pa = [r[5] for r in tr[s] if r[1] <= f and t - r[0] <= 5 and r[5] != '0x12']; pb = [r[5] for r in tr[o] if r[1] <= f]
    po = (pa[-1] if pa else pre.get((s, f - 1), ('?', '?'))[0], pb[-1] if pb else '?')
    def mv(x, order, tt, brake):  # constant velocity, or braking to rest at DEC along the velocity on a stopping order (0, 0x1f, 0x21)
        if not brake or order not in ('0x0', '0x1f', '0x21') or x['spd'] < 1e-3: return add(x['c'], x['v'], tt)
        ts = x['spd'] / DEC; u = min(tt, ts); k = (u - 0.5 * DEC * u * u / x['spd']); return add(x['c'], x['v'], k)
    res = []
    for brake in (False, True):
        best = (1e9, 0); g0 = None
        for k in range(61):
            tt = 0.5 * k
            g = cap_gap(mv(a, po[0], tt, brake), a['f'], S[s], mv(b, po[1], tt, brake), b['f'], S[o]) - (50 + 10 * tt)
            if k == 0: g0 = g + 50
            if g < best[0]: best = (g, tt)
        res.append(best)
    best = res[0]
    fl = (1e9, 0); n = 0
    for r in tr[s]:
        if t <= r[0] <= t + 30:
            q = at(o, r[0])
            if q: n += 1; g = cap_gap(r[2], r[3], S[s], q[0], q[1], S[o]); fl = min(fl, (g, r[0] - t))
    ok = abs(dc - float(d['d_m'])) <= 100
    cls = lambda x: 'clear by sweep' if x[0] >= 0 else 'converging'
    v = ('const-v %s / braking %s' % (cls(res[0]), cls(res[1]))) if ok else 'not determinable from the rows'
    print('%.1f %s(pre %s, %.0f m/s age %.1f) vs %s(pre %s, %.0f m/s age %.1f) | d %.0f vs %s | gap now %.0f; sweep min const-v %.0f at %.1f s, braking %.0f at %.1f s | flown %s | %s' % (
        t, s[-4:], po[0], a['spd'], a['age'], o[-4:], po[1], b['spd'], b['age'], dc, d['d_m'], g0, best[0], best[1], res[1][0], res[1][1],
        ('%.0f at +%.1f s (%d frames)' % (fl[0], fl[1], n)) if n else 'B untraced', v))
# the closest approach over the trip: pairs traced at the same time, and site E's minimum centre distance per pair
print('closest centre distance per pair from site E verdict rows (A,B both of the four):')
for k, (dm, f) in sorted(ev_min.items()): print('  %s-%s %.0f m at f=%d g=%.1f' % (k[0][-4:], k[1][-4:], dm, f, f2t.get(f, 0)))
names = sorted(S)
for i in range(4):
    for j in range(i + 1, 4):
        s, o = names[i], names[j]; best = (1e9, 0, 0); n = 0
        for r in tr[s]:
            q = at(o, r[0])
            if q:
                n += 1; g = cap_gap(r[2], r[3], S[s], q[0], q[1], S[o]); dc = math.dist(r[2], q[0])
                if g < best[0]: best = (g, r[0], dc)
        if n: print('  traced together %s-%s: %d frames, min capsule gap %.0f m at g=%.1f (centre %.0f m)' % (s[-4:], o[-4:], n, best[0], best[1], best[2]))
