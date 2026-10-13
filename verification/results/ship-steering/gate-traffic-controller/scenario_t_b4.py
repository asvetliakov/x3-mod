"""B.4: scenario T's order-0 gap check for the convoy's small escorts, summed per variant from the fixture's B.4 lines.
Usage: python3 -I scenario_t_b4.py verification/results/ship-steering/ship_steering_host.txt"""
import re
import sys

ROW = re.compile(r'^traffic T (.+?)\s+fps=\s*(\d+) seed=(\d+) B\.4: capital vs own small escort in the order-0 gap: warns=(\d+) '
                 r'unsuppressed=(\d+) \(B\.3 order: (\d+)\) a_age_max_ms=(\d+)')
agg = {}
for line in open(sys.argv[1]):
    m = ROW.match(line)
    if not m:
        continue
    a = agg.setdefault(m.group(1), [0, 0, 0, 0, 0])
    a[0] += 1
    for k in range(3):
        a[k + 1] += int(m.group(4 + k))
    a[4] = max(a[4], int(m.group(7)))
print('variant        runs  gap warns  unsuppressed  unsuppressed under B.3 order  A age max ms')
tot = [0, 0, 0, 0, 0]
for tag, a in agg.items():
    print('%-14s %4d %10d %13d %29d %13d' % (tag, *a))
    for k in range(4):
        tot[k] += a[k]
    tot[4] = max(tot[4], a[4])
print('%-14s %4d %10d %13d %29d %13d' % ('total', *tot))
print('RESULT %s' % ('PASS' if tot[2] == 0 and tot[0] == 72 else 'FAIL'))
