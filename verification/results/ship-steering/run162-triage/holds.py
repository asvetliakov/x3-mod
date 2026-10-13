"""Run161 Q3: capital order-0 holds from cap_orders.txt: order-0 runs >= 8 game s that are densely sampled (n >= 3, maxgap <= 2 s);
sparse runs (maxgap > 2 s) are reported apart as sampling gaps, not holds. Usage: python3 -I holds.py cap_orders.txt"""
import re, sys, collections as C
cur = None; H = C.defaultdict(list); G = C.defaultdict(list)
for l in open(sys.argv[1]):
    m = re.match(r'== (\S+) (\S+)', l)
    if m: cur = m[1] + ' ' + m[2]; continue
    m = re.match(r'\s+0x0\s+(\d+)-(\d+) game ([\d.]+) dur ([\d.]+) n (\d+) maxgap ([\d.]+)', l)
    if m and float(m[4]) >= 8:
        (H if int(m[5]) >= 3 and float(m[6]) <= 2 else G)[cur].append((int(m[1]), float(m[4])))
print(sys.argv[1], 'holds (>= 8 s, dense):', sum(len(v) for v in H.values()), 'total %.1f s' % sum(d for v in H.values() for _, d in v), dict(H))
print('   sparse runs (not counted):', dict(G))
