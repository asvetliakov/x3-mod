#!/usr/bin/env python3
"""A' only (dilated camera chain removed, 2026-09-24): the rows of two run_temporal_pass.py reports (temporal-pass.txt or
temporal-lattice.txt) that differ, wall-clock rows (TIMING, *_ms=) left out, counted by row name; rows only in one report
are counted as removed / added.

    python3 verification/results/aprime-only/report_diff.py BEFORE/temporal-pass.txt AFTER/temporal-pass.txt"""
import collections
import sys


def rows(path):
    return [l.rstrip('\n') for l in open(path) if 'TIMING' not in l and '_ms=' not in l]


before, after = rows(sys.argv[1]), rows(sys.argv[2])
old, new = collections.Counter(before), collections.Counter(after)
gone, came = old - new, new - old
key = lambda line: line.split(' ', 1)[0]
removed, added = collections.Counter(), collections.Counter()
for line, n in gone.items():
    removed[key(line)] += n
for line, n in came.items():
    added[key(line)] += n
print(f'lines before={len(before)} after={len(after)} identical={sum((old & new).values())}')
for name in sorted(set(removed) | set(added)):
    print(f'{name:40s} only_before={removed[name]:4d} only_after={added[name]:4d}')
