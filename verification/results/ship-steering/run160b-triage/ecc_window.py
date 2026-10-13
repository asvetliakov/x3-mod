# Run160b Q4: capital-capital E verdicts per unordered pair and (A_order,B_order) in a frame window, with first/last frame.
# Usage: python3 -I ecc_window.py rows.txt f0 f1
import sys, re, collections as C
caps = {l.split()[0]: l.split()[1] for l in open('caps.txt')}
f0, f1 = int(sys.argv[2]), int(sys.argv[3]); c = C.defaultdict(lambda: [0, 10**9, 0])
for l in open(sys.argv[1], errors='replace'):
    if not l.startswith('ship_avoid_e_verdict '): continue
    d = dict(re.findall(r'(\w+)=(\S+)', l)); f = int(d['frame'])
    if f0 <= f <= f1 and d['A'] in caps and d['B'] in caps:
        k = (tuple(sorted((d['A'], d['B']))), d['A_order'] + '/' + d['B_order'], d['verdict']); v = c[k]; v[0] += 1; v[1] = min(v[1], f); v[2] = max(v[2], f)
for k, v in sorted(c.items(), key=lambda kv: kv[1][1]): print(k[0][0], k[0][1], k[1], k[2], 'rows', v[0], 'frames %d-%d' % (v[1], v[2]))
