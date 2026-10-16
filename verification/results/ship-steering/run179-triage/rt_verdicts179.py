# Run179 copy (ships from argv[2], comma-separated) of run178-triage/rt_verdicts178.py. Run178 Q1: site E's verdict on each 0x12 episode among the four: the warn_pre row (the call before the warn; A's order, B's
# entry and e_verdict) nearest at or before the warn frame, and the site E verdict rows for the (A, B) pair within 5 s before it.
# Usage: python3 -I rt_verdicts179.py ROWS SHIP,SHIP,...
import sys, re, bisect, collections as C
S = sys.argv[2].split(',')
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
f2t = {}; pre = C.defaultdict(list); ev = C.defaultdict(list); warns = []
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        m = re.match(r'ship_trace frame=(\d+) t_ms=(\d+)', l); f2t.setdefault(int(m[1]), int(m[2]) / 1e3)
    elif l.startswith('ship_avoid_engine_warn_pre '):
        d = kv(l)
        if d['obj'] in S:
            for e in l.split('e_verdict=')[1].split()[0].split(';'):
                x = e.split(':')
                if x[0] in S: pre[(d['obj'], x[0])].append((int(d['frame']), d['order'], x[3], x[8], l.split('obst_row=')[1].split()[0]))
    elif l.startswith('ship_avoid_e_verdict '):
        d = kv(l)
        if d['A'] in S and d['B'] in S: ev[(d['A'], d['B'])].append((int(d['frame']), d['verdict']))
    elif l.startswith('ship_avoid_engine_warn '):
        d = kv(l)
        if d['obj'] in S and d['other'] in S: warns.append(d)
fk = sorted(f2t); T = lambda f: f2t[fk[min(bisect.bisect_left(fk, f), len(fk) - 1)]]
last = {}
for d in warns:
    s, f, o = d['obj'], int(d['frame']), d['other']
    if last.get(s, -10) >= f - 3: last[s] = f; continue
    last[s] = f; t = T(f)
    p = [x for x in pre[(s, o)] if x[0] <= f]; p = p[-1] if p else None
    v = C.Counter(x[1] for x in ev[(s, o)] if t - 5 <= T(x[0]) <= t)
    print('%.1f A=%s B=%s | warn_pre %s | site E verdicts on (A,B) in the 5 s before: %s' % (t, s[-4:], o[-4:],
          ('f=%d (-%d frames) A order %s B order %s e_verdict %s obst_row %s' % (p[0], f - p[0], p[1], p[2], p[3], p[4])) if p else 'none', dict(v) or 'none'))
