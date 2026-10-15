# Summarises far_bound_c.sh's runs: per build (off: committed, on: -DX3M_LANE_FAR=1) and cast, the spans (3 seeds x tf 1 / 6 / 10), the
# Fx2 figures beyond 20 km (lane-write frames, steer calls, rewrites, floor-crawl frames), the last launch against Fx's derived bound, and
# the failed checks by name. Usage: python3 -I far_bound_c.py <dir of far_bound_c.sh's outputs>
import collections, os, re, sys
d = sys.argv[1]
for b in ('off', 'on'):
    for c in ('third_gate', 'side_commit'):
        t = open(os.path.join(d, '%s_%s.txt' % (b, c))).read() + '\n' + open(os.path.join(d, '%s_%s.err' % (b, c))).read()
        spans = re.findall(r'scenario C %s lane tf (\d+) \d+ fps seed (\d+): launched (\d+/\d+) span ([-0-9.]+)' % c, t)
        far = re.findall(r'scenario C %s far tf (\d+) seed (\d+): w (\d+) steer (\d+) rewrites (\d+) crawl (\d+) \| last_launch ([-0-9.]+) bound ([-0-9.]+)' % c, t)
        fails = collections.Counter(m for m in re.findall(r'FAIL scenario C %s tf \d+ seed \d+: (\w+ \w+ \w+ \w+)' % c, t))
        print('%s %-11s spans %s' % (b, c, ' '.join('tf%s/s%s:%s' % (x[0], x[1], x[3]) for x in spans)))
        print('%s %-11s far(w/steer/rewrites/crawl last/bound) %s' % (b, c, ' '.join('tf%s/s%s:%s/%s/%s/%s %s/%s' % x for x in far)))
        print('%s %-11s failed %s' % (b, c, ', '.join('%s x%d' % kv for kv in sorted(fails.items())) or 'none'))
