# Section 13.1's distance condition (convoy-gate-transit.md section 13.11 "Distance condition"): laneon_diff.py's before-core (--base's
# src/proxy headers) vs after (the working tree) stdouts, normalised by laneon_diff.norm (host timings out), differing lines counted per
# cast tag. Every cast is expected at 0 but rt_far (its far arrival is vanilla on the base core, a lane member after) and the totals line:
# the arrival-side casts' return orders come at the gate, inside the 15 km commit sphere, so the latch is unchanged there.
#   python3 -I laneon_diff.py --base 67db4e3d --out DIR; python3 -I arr_far/laneon_by_cast.py DIR
import collections, difflib, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import laneon_diff as L
EXPECTED = {'rt_far', 'checks', 'ship_steering_host'}
d = sys.argv[1]
bad = 0
for a, b in (('before.txt', 'after.txt'), ('before__lane.txt', 'after__lane.txt'), ('before__capital.txt', 'after__capital.txt'),
             ('base__lane.txt', 'after__lane.txt')):  # (the last: --base's own fixture and core, lane-on rows, laneon_diff's comparison 2)
    lo = a.startswith('base')
    x, y = L.norm(os.path.join(d, a), lo), L.norm(os.path.join(d, b), lo)
    c = collections.Counter()
    for l in difflib.unified_diff(x, y, lineterm='', n=0):
        if l.startswith(('+++', '---')) or not l.startswith(('+', '-')):
            continue
        m = re.match(r'[+-]\s*(?:FAIL )?(?:scenario C )?(\S+)', l)
        c[m.group(1) if m else '?'] += 1
    other = {k: v for k, v in c.items() if k not in EXPECTED}
    bad += sum(other.values())
    print('%s vs %s: lines %d / %d; expected differing %s; every other cast differing %s' % (
        a, b, len(x), len(y), dict(sorted((k, v) for k, v in c.items() if k in EXPECTED)), dict(sorted(other.items())) or 0))
sys.exit(1 if bad else 0)
