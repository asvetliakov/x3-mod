"""Final review item 6 (convoy-gate-transit.md section 11): the lane cost bench figures as the committed fixture outputs carry them.
Each `lane cost (host)` row is one bench: 3 runs per key, `worst frame` = the worst frame of the 3 runs, `third-worst` = the largest
of the 3 runs' third-worst frames (the checked figure, against the 1 ms ceiling). The key-off half is stage B's before 2026-10-15 and
the stage-A certificate's after it ('key off' in the row). Prints per cast and time factor the range over the
files' rows. Usage: python3 -I bench_figures.py <fixture stdout>... (no Wine, no game)."""
import re, sys
from collections import defaultdict
ROW = re.compile(r'lane cost \(host\) (\S+) tf (\d+): lane ([\d.]+) us mean, (\d+) us worst frame, (\d+) us third-worst \((\d+) decisions.*?(?:stage B|key off) '
                 r'([\d.]+) us mean, (\d+) us worst, (\d+) us third')
fig = defaultdict(list)
for path in sys.argv[1:]:
    for line in open(path):
        m = ROW.search(line)
        if m:
            fig[(m.group(1), int(m.group(2)))].append((path.rsplit('/', 1)[-1],) + tuple(float(x) for x in m.groups()[2:]))
for (cast, tf), rows in sorted(fig.items()):
    def rng(i):
        v = [r[i] for r in rows]
        return '%g-%g' % (min(v), max(v))
    print('%s tf %d (%d rows): lane mean %s us, worst %s us, third-worst %s us | key off mean %s, worst %s, third %s' % (
        cast, tf, len(rows), rng(1), rng(2), rng(3), rng(5), rng(6), rng(7)))
    for r in rows:
        print('  %-36s lane %.1f / %d / %d  key off %.1f / %d / %d' % (r[0], r[1], r[2], r[3], r[5], r[6], r[7]))
