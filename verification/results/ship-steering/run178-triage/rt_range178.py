# Run178 Q3: the pair's centre distance around each 0x12 episode from every row that carries it (site E verdict d_m, engine warn
# d_m, warn_pre entries), either direction: d 5 s and 2 s before the warn, at the warn, the closing rate over the 2 s before, and the
# minimum over the next 30 s with its time (the 0x12 flown), and the orientation-free sweep bound: with constant velocities the centre
# distance is >= d0 + rate t (the radial projection), so the capsule gap - margin is >= d0 + rate t - (hA + hB + wA + wB) - (50 + 10 t);
# if that stays >= 0 over 0..30 s the capsule sweep clears the pair whatever the noses (h = L - w; M2 1092/583, M1 1022/546). Usage: python3 -I rt_range178.py ROWS
import sys, re, bisect
S = {'0x8fe612b8', '0x8fe58420', '0x39cfa520', '0x39cf2180'}
HULL = {'0x8fe612b8': (1092, 583), '0x8fe58420': (1092, 583), '0x39cfa520': (1092, 583), '0x39cf2180': (1022, 546)}
ext = lambda x: (HULL[x][0] - HULL[x][1]) + HULL[x][1]
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
f2t = {}; dd = {}; warns = []
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        m = re.match(r'ship_trace frame=(\d+) t_ms=(\d+)', l); f2t.setdefault(int(m[1]), int(m[2]) / 1e3)
    elif l.startswith('ship_avoid_e_verdict '):
        d = kv(l)
        if d['A'] in S and d['B'] in S: dd.setdefault(frozenset((d['A'], d['B'])), []).append((int(d['frame']), float(d['d_m'])))
    elif l.startswith('ship_avoid_engine_warn '):
        d = kv(l)
        if d['obj'] in S and d['other'] in S:
            dd.setdefault(frozenset((d['obj'], d['other'])), []).append((int(d['frame']), float(d['d_m']))); warns.append(d)
fk = sorted(f2t)
T = lambda f: f2t[fk[min(bisect.bisect_left(fk, f), len(fk) - 1)]]
last = {}
for d in warns:
    s, f, o = d['obj'], int(d['frame']), d['other']
    if last.get(s, -10) >= f - 3: last[s] = f; continue
    last[s] = f
    t = T(f); ser = sorted((T(g), x) for g, x in dd[frozenset((s, o))])
    def near(tt):
        c = [p for p in ser if abs(p[0] - tt) <= 1.0]
        return min(c, key=lambda p: abs(p[0] - tt)) if c else None
    b5, b2 = near(t - 5), near(t - 2); after = [p for p in ser if t <= p[0] <= t + 30]
    mn = min(after, key=lambda p: p[1]) if after else None
    rate = (float(d['d_m']) - b2[1]) / (t - b2[0]) if b2 and t > b2[0] else None
    lb = None
    if rate is not None:
        E = ext(s) + ext(o); lb = min(float(d['d_m']) + rate * k * 0.5 - E - (50 + 10 * k * 0.5) for k in range(61))
    print('%.1f %s vs %s: d -5 s %s, -2 s %s, warn %s, rate %s m/s | radial sweep bound min %s -> %s | min next 30 s %s (%d rows)' % (
        t, s[-4:], o[-4:], '%.0f' % b5[1] if b5 else '-', '%.0f' % b2[1] if b2 else '-', d['d_m'],
        '%.1f' % rate if rate is not None else '-', '%.0f' % lb if lb is not None else '-', ('clear by sweep' if lb is not None and lb >= 0 else 'not proven'), '%.0f at +%.1f s' % (mn[1], mn[0] - t) if mn else '-', len(after)))
