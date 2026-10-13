# Run160b Q3: distance to a gate over time for any capital from E rows (A = capital, B = the gate), sampled every ~5 game s,
# with A_order; radial speed from successive samples. Usage: python3 -I gate_dist.py rows.txt gate f0 f1 obj...
import sys, re, bisect
gate, f0, f1, objs = sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), set(sys.argv[5:])
tm = {}; S = {o: [] for o in objs}
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('ship_trace frame='):
        m = re.match(r'ship_trace frame=(\d+) t_ms=(\d+)', l); tm.setdefault(int(m[1]), int(m[2]))
    elif l.startswith('ship_avoid_e_verdict ') and ' B=' + gate + ' ' in l:
        d = dict(re.findall(r'(\w+)=(\S+)', l)); f = int(d['frame'])
        if d['A'] in objs and f0 <= f <= f1: S[d['A']].append((f, float(d['d_m']), d['A_order'], d['verdict']))
fs = sorted(tm)
def g(f): i = min(bisect.bisect_left(fs, f), len(fs) - 1); return tm[fs[i]] / 1e3
for o, rs in S.items():
    out = []; last = None
    for f, d, od, v in rs:
        if last is None or g(f) - g(last[0]) >= 5:
            sp = (d - last[1]) / (g(f) - g(last[0])) if last else 0
            out.append('%.0f:%s:%.0fm:%+.0f' % (g(f), od, d, sp)); last = (f, d)
    print('==', o, 'rows', len(rs)); print('  ', ' '.join(out))
