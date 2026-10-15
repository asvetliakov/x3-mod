# The arrival mode's laneon_diff by cast (convoy-gate-transit.md section 13 "Implementation", item E): laneon_diff.py's before-core
# (--base's src/proxy headers) vs after (the working tree) stdouts, normalised by laneon_diff.norm (host timings out), differing lines
# counted per cast tag; the departure-side casts are expected at 0, the arrival-side casts (a return trip's leader is a mode member:
# the mode casts and return_pass) and the totals line differ.
#   python3 -I laneon_diff.py --base 1c563443 --out DIR; python3 -I arr_mode/laneon_by_cast.py DIR
import collections, difflib, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import laneon_diff as L
MODE = {'return_trip', 'return_pass', 'rt_trail', 'rt_unread', 'rt_titan', 'rt_titan_rest', 'rt_hub', 'rt_cross', 'checks', 'ship_steering_host'}
d = sys.argv[1]
bad = 0
for a, b in (('before.txt', 'after.txt'), ('before__lane.txt', 'after__lane.txt'), ('before__capital.txt', 'after__capital.txt')):
    x, y = L.norm(os.path.join(d, a)), L.norm(os.path.join(d, b))
    c = collections.Counter()
    for l in difflib.unified_diff(x, y, lineterm='', n=0):
        if l.startswith(('+++', '---')) or not l.startswith(('+', '-')):
            continue
        m = re.match(r'[+-]\s*(?:FAIL )?(?:scenario C )?(\S+)', l)
        c[m.group(1) if m else '?'] += 1
    dep = {k: v for k, v in c.items() if k not in MODE}
    bad += sum(dep.values())
    print('%s vs %s: lines %d / %d; arrival-side differing %s; departure-side differing %s' % (
        a, b, len(x), len(y), dict(sorted((k, v) for k, v in c.items() if k in MODE)), dict(sorted(dep.items())) or 0))
sys.exit(1 if bad else 0)
