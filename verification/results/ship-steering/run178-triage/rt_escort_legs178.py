# Run178 Q1: the escorts' 0x14 legs toward their leader 0x39cfa520 (escort orders from every row carrying them: site E A_order, trace,
# warn rows): per leg start/end, its centre distance to the leader at start and end (nearest pair d_m row within 1 s), the leader's order
# at the leg's end, and whether the leg's end (the order-0 gap) is followed by a 0x12 within 3 s. Usage: python3 -I rt_escort_legs178.py ROWS
import sys, re, bisect
L = '0x39cfa520'; E = ['0x8fe58420', '0x39cf2180']
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
f2t = {}; od = {e: [] for e in E}; dd = {e: [] for e in E}; lo = []
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        d = kv(l); f = int(d['frame']); f2t.setdefault(f, int(d['t_ms']) / 1e3)
        if d['obj'] in od: od[d['obj']].append((f, d['order']))
        if d['obj'] == L: lo.append((f, d['order']))
    elif l.startswith('ship_avoid_e_verdict '):
        d = kv(l); f = int(d['frame'])
        if d['A'] in od: od[d['A']].append((f, d['A_order']))
        if d['A'] == L: lo.append((f, d['A_order']))
        if d['B'] == L and d['A'] in dd: dd[d['A']].append((f, float(d['d_m'])))
        if d['A'] == L and d['B'] in dd: dd[d['B']].append((f, float(d['d_m'])))
    elif l.startswith('ship_avoid_engine_warn '):
        d = kv(l); f = int(d['frame'])
        if d['obj'] in od: od[d['obj']].append((f, '0x12'))
fk = sorted(f2t); T = lambda f: f2t[fk[min(bisect.bisect_left(fk, f), len(fk) - 1)]]
lo.sort()
def lead_order(f):
    i = bisect.bisect_right([x[0] for x in lo], f) - 1; return lo[i][1] if i >= 0 else '?'
for e in E:
    seq = sorted(od[e]); runs = []
    for f, o in seq:
        if runs and runs[-1][0] == o: runs[-1][2] = f
        else: runs.append([o, f, f])
    ser = sorted((T(f), x) for f, x in dd[e])
    def dat(t):
        c = [p for p in ser if abs(p[0] - t) <= 1.0]; return '%.0f' % min(c, key=lambda p: abs(p[0] - t))[1] if c else '-'
    for i, (o, f0, f1) in enumerate(runs):
        if o != '0x14': continue
        nxt = runs[i + 1][0] if i + 1 < len(runs) else '-'; nxt2 = runs[i + 2][0] if i + 2 < len(runs) else '-'
        t0, t1 = T(f0), T(f1)
        x12 = 'yes' if nxt == '0x12' or (nxt == '0x0' and nxt2 == '0x12' and T(runs[i + 2][1]) - t1 <= 3) else 'no'
        print('%s 0x14 %.1f-%.1f (%.1f s) d to leader %s -> %s | leader order at end %s | then %s,%s | 0x12 within 3 s: %s' % (
            e[-4:], t0, t1, t1 - t0, dat(t0), dat(t1), lead_order(f1), nxt, nxt2, x12))
