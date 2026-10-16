# Run178 Q1/Q3: trace coverage of the four return-trip ships per 0x12 episode (first engine warn row of each episode).
# Usage: python3 -I rt_coverage178.py ROWS
import sys, re, bisect
S = ['0x8fe612b8', '0x8fe58420', '0x39cfa520', '0x39cf2180']
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
tr = {s: [] for s in S}; warns = []; fr_t = {}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        m = re.match(r'ship_trace frame=(\d+) t_ms=(\d+) obj=(\S+)', l)
        fr_t.setdefault(int(m[1]), int(m[2]))
        if m[3] in tr: tr[m[3]].append((int(m[1]), int(m[2])))
    elif l.startswith('ship_avoid_engine_warn '):
        d = kv(l)
        if d['obj'] in tr and d['other'] in tr: warns.append(d)
last = {}
for d in warns:
    s, f = d['obj'], int(d['frame'])
    if last.get(s, -10) >= f - 3: last[s] = f; continue
    last[s] = f
    o = d['other']
    def near(x):
        fs = [a for a, _ in tr[x]]; i = bisect.bisect_left(fs, f)
        c = [tr[x][j] for j in (i - 1, i) if 0 <= j < len(fs)]
        return min((abs(a - f), a, b) for a, b in c) if c else None
    na, nb = near(s), near(o)
    print('warn f=%d A=%s B=%s d_m=%s | A nearest trace frame %s (df %d) | B nearest trace frame %s (df %d)' % (f, s, o, d['d_m'], na[1], na[0], nb[1], nb[0]))
