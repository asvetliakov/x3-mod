#!/usr/bin/env python3
"""compare.py <before.txt> <after.txt>: measure.py tables side by side as markdown rows (case, element: per 40 / 120 /
296 / 696 ms the equivalent 10 %-of-peak radius px and the peak, before -> after; '-' where a side has no row)."""
import sys
def load(p):
    out = {}
    for line in open(p):
        f = line.split()
        if len(f) < 6 or f[0] == 'case':
            continue
        out[(f[0], f[2], int(f[1]))] = (float(f[5]), float(f[3]))
    return out
before, after = load(sys.argv[1]), load(sys.argv[2])
keys = sorted({(c, e) for c, e, _ in after})
for c, e in keys:
    cells = []
    for ms in (40, 120, 296, 696):
        b, a = before.get((c, e, ms)), after.get((c, e, ms))
        fmt = lambda v: f'{v[0]:.0f} / {v[1]:.2f}' if v else '-'
        cells.append(f'{fmt(b)} -> {fmt(a)}' if b else fmt(a))
    print(f'| {c} {e} | ' + ' | '.join(cells) + ' |')
