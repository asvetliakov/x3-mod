"""Round A: the scenario C spans (15 km -> last launch, min..max over 3 seeds) per cast and time factor, before and after, with the
largest shift of either end (s), from two --lane stdouts; also the merge completion maxima and the closest capsule per cast.
Usage: python3 -I spans_diff.py <before stdout> <after stdout> [--md]."""
import re
import sys
from collections import defaultdict

SPANS = re.compile(r'^scenario C (\S+) spans \(s, 15 km -> last launch\): tf1 (-?[\d.]+)\.\.(-?[\d.]+) tf6 (-?[\d.]+)\.\.(-?[\d.]+) tf10 (-?[\d.]+)\.\.(-?[\d.]+)$')
ROW = re.compile(r'^scenario C (\S+) lane tf (\d+) \d+ fps seed (\d+): (.*)$')


def load(path):
    spans, merge, caps, eps = {}, defaultdict(float), defaultdict(lambda: 1e18), defaultdict(int)
    with open(path, encoding='utf-8', errors='replace') as f:
        for l in f:
            m = SPANS.match(l)
            if m:
                spans[m.group(1)] = tuple(float(x) for x in m.groups()[1:])
                continue
            m = ROW.match(l)
            if m:
                c, r = m.group(1), m.group(4)
                mm = re.search(r'merges \d+ done \d+ max ([\d.]+) s', r)
                if mm:
                    merge[c] = max(merge[c], float(mm.group(1)))
                mc = re.search(r'capsule (-?\d+)', r)
                if mc:
                    caps[c] = min(caps[c], float(mc.group(1)))
                me = re.search(r'episodes (\d+) uncert (\d+)', r)
                if me:
                    eps[c] += int(me.group(1)) + int(me.group(2))
    return spans, merge, caps, eps


before, after = load(sys.argv[1]), load(sys.argv[2])
md = '--md' in sys.argv
worst = 0.0
if md:
    print('| Cast | tf 1 before | tf 1 after | tf 6 before | tf 6 after | tf 10 before | tf 10 after | largest shift (s) | merge max before/after (s) | capsule before/after (m) |')
    print('| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |')
for c in after[0]:
    if c not in before[0]:
        continue
    b, a = before[0][c], after[0][c]
    shift = max(abs(a[i] - b[i]) for i in range(6) if a[i] >= 0 and b[i] >= 0) if any(a[i] >= 0 and b[i] >= 0 for i in range(6)) else 0.0
    worst = max(worst, shift)
    cells = ['%.1f..%.1f' % (b[0], b[1]), '%.1f..%.1f' % (a[0], a[1]), '%.1f..%.1f' % (b[2], b[3]), '%.1f..%.1f' % (a[2], a[3]),
             '%.1f..%.1f' % (b[4], b[5]), '%.1f..%.1f' % (a[4], a[5])]
    mg = '%.1f / %.1f' % (before[1][c], after[1][c])
    cp = '%.0f / %.0f' % (before[2][c] if before[2][c] < 1e17 else -1, after[2][c] if after[2][c] < 1e17 else -1)
    if md:
        print('| %s | %s | %.1f | %s | %s |' % (c, ' | '.join(cells), shift, mg, cp))
    else:
        print('%-15s before %s after %s | shift %.1f s | merge %s | capsule %s | episodes+uncert %d -> %d' % (
            c, ' '.join(cells[0::2]), ' '.join(cells[1::2]), shift, mg, cp, before[3][c], after[3][c]))
print(('largest shift over every cast and time factor: %.1f s' % worst) if not md else ('\nLargest shift over every cast and time factor: %.1f s.' % worst))
