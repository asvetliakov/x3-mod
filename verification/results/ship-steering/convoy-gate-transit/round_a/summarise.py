"""Round A (convoy-gate-transit.md section 11 "Round A"): one fixture run's summary from its stdout and stderr: the checks line, the
failures grouped by cast, time factor and check (host-timing bench checks left out), the per-cast spans, the engine-path rows of the
casts named and the lane cost rows. Usage: python3 -I summarise.py <stdout> <stderr> [cast ...]."""
import re
import sys
from collections import Counter

out, err = sys.argv[1], sys.argv[2]
casts = sys.argv[3:]
with open(out, encoding='utf-8', errors='replace') as f:
    lines = f.read().splitlines()
with open(err, encoding='utf-8', errors='replace') as f:
    elines = f.read().splitlines()
for l in lines:
    if l.startswith('ship_steering_host checks='):
        print(l)
fails = Counter()
for l in elines:
    m = re.match(r'^FAIL scenario C ([a-z0-9_]+) tf (\d+) seed \d+: (.*)$', l)
    if m:
        fails[(m.group(1), int(m.group(2)), re.sub(r' \(.*', '', m.group(3)))] += 1
    elif l.startswith('FAIL') and 'lane cost' not in l:
        fails[('-', 0, l[:120])] += 1
for (c, tf, what), n in sorted(fails.items()):
    print('  FAIL %d %s tf%d: %s' % (n, c, tf, what))
for l in lines:
    m = re.match(r'^scenario C ([a-z0-9_]+) spans \(s, 15 km -> last launch\): (.*)$', l)
    if m and (not casts or m.group(1) in casts):
        print('  spans %-15s %s' % (m.group(1), m.group(2)))
for l in lines:
    m = re.match(r'^scenario C ([a-z0-9_]+) engine paths (.*)$', l)
    if m and m.group(1) in casts:
        print('  eng %s %s' % (m.group(1), m.group(2)[:400]))
for l in lines:
    if l.startswith('lane cost (host)') or l.startswith('ctl cost (host): a decision'):
        print('  ' + l[:230])
