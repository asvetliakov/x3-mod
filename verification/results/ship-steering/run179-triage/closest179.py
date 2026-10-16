# Run179 Q1/Q2 contacts: per unordered pair with A among the given ships, the minimum centre distance d_m over site E verdict rows
# (ship_avoid_e_verdict, every pair the engine's warn scan visits) in a frame window, with r_B and the frame/time of the minimum.
# The verdict rows only exist for scanned pairs (inside the engine's warn reach), so a pair absent here never came that close.
# Prints the 25 closest pairs, then every pair with both ships in the set.
# Usage: python3 -I closest179.py ROWS SHIP,SHIP,... LO_FRAME HI_FRAME
import sys, re, bisect
rows, S, lo, hi = sys.argv[1], set(sys.argv[2].split(',')), int(sys.argv[3]), int(sys.argv[4])
kv = lambda l: dict(re.findall(r'(\w+)=(\S+)', l))
f2t = {}; best = {}
for l in open(rows, errors='replace'):
    m = re.match(r'ship_\S+ frame=(\d+) t_ms=(\d+)', l)
    if m: f2t.setdefault(int(m[1]), int(m[2]))
    if not l.startswith('ship_avoid_e_verdict '): continue
    d = kv(l); f = int(d['frame'])
    if not lo <= f <= hi or d['A'] not in S: continue
    k = tuple(sorted((d['A'], d['B']))); dm = float(d['d_m'])
    if k not in best or dm < best[k][0]: best[k] = (dm, f, d['r_B'], d['B_code'], d['verdict'])
fk = sorted(f2t)
T = lambda f: f2t[fk[min(bisect.bisect_left(fk, f), len(fk) - 1)]] / 1e3
for k, (dm, f, rb, c, v) in sorted(best.items(), key=lambda x: x[1][0])[:25]:
    print('%s-%s min d %.0f m at f=%d g=%.1f (r_B %s %s, verdict %s)' % (k[0], k[1], dm, f, T(f), rb, c, v))
print('# pairs within the set')
for k, (dm, f, rb, c, v) in sorted(best.items(), key=lambda x: x[1][0]):
    if k[0] in S and k[1] in S: print('%s-%s min d %.0f m at f=%d g=%.1f (r_B %s %s, verdict %s)' % (k[0], k[1], dm, f, T(f), rb, c, v))
