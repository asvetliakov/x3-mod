#!/usr/bin/env python3
"""Counts rows per leading key whose text matches refus|error|fail|warn|overflow=[1-9]|status=(?!ok) etc.,
plus nonzero *_refused= counters, plus motion_output_taa_depth_fold rows. Usage: warn_rows.py LOG"""
import re, sys, collections
pat = re.compile(r'(refus\w*|error\w*|fail\w*|warn\w*|reject\w*|invalid\w*|abort\w*)(=([^ \n]+))?', re.I)
c = collections.Counter(); ex = {}
fold = collections.Counter()
for l in open(sys.argv[1], errors='replace'):
    k = l.split(' ', 1)[0]
    if k.startswith('motion_output_taa_depth_fold'):
        fold[re.sub(r'=\S+', '=', l.strip())[:160]] += 1
    for m in pat.finditer(l):
        name, val = m.group(1).lower(), m.group(3)
        if val is not None and re.fullmatch(r'0|none|0x0+|-1|ok|0\.0+', val): continue
        key = (k, name if val is None else f'{name}!=0'); c[key] += 1; ex.setdefault(key, l.strip()[:220])
for (k, n), v in sorted(c.items()): print(f'{v:7d} {k} {n}  e.g. {ex[(k, n)]}')
print('-- fold rows (values stripped):')
for s, v in fold.items(): print(f'{v:7d} {s}')
