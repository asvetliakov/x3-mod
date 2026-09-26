#!/usr/bin/env python3
"""Reads draw_table.py output (stdin) and prints per frame: draw count, the sequence of clears (C<flags>) and bullet
draws (B<index>, pair 5e484a06/ec1f5c4a), and the first/last depth-writing (ZW=1) draw index.
usage: draw_table.py LOG FRAMES... | order_summary.py"""
import sys, re, collections
fr = collections.OrderedDict()
for l in sys.stdin:
    if l.startswith('{'):
        if "'clear'" in l:
            f = re.search(r"'frame': '(\d+)'", l).group(1); fr.setdefault(f, []).append('C' + re.search(r"'flags': '(\d+)'", l).group(1))
        continue
    f = l.split()[0]; idx = int(re.search(r' d\s*(\d+)', l).group(1))
    fr.setdefault(f, [])
    if 'vs=5e484a06 ps=ec1f5c4a' in l: fr[f].append(f'B{idx}')
    if 'ZW=1' in l: fr[f].append(('Z', idx))
    fr[f].append(('D', idx))
for f, seq in fr.items():
    z = [i for k, i in (s for s in seq if isinstance(s, tuple)) if k == 'Z']
    n = sum(1 for s in seq if isinstance(s, tuple) and s[0] == 'D')
    print(f, 'draws', n, 'clears/bullets', [s for s in seq if isinstance(s, str)], 'ZW=1 first', z[0] if z else None, 'last', z[-1] if z else None, 'n', len(z))
