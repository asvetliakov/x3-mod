"""The bench24 lane-cost lines of laneon_diff.py's kept --lane stdouts (laneon_diff.py --out DIR): mean, worst and third-worst frame
at tf 1 and tf 10 per build.   python3 -I bench_lines.py DIR"""
import os, re, sys

d = sys.argv[1]
for k in ('before', 'base', 'after'):
    p = os.path.join(d, k + '__lane.txt')
    if not os.path.exists(p):
        continue
    row = []
    for line in open(p):
        m = re.match(r'lane cost \(host\) bench24 tf (\d+): lane ([0-9.]+) us mean, (\d+) us worst frame, (\d+) us third-worst', line)
        if m:
            row.append('tf %s: mean %s us, worst %s us, third-worst %s us' % m.groups())
    print('%-7s %s' % (k, ' | '.join(row)))
