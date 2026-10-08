#!/usr/bin/env python3
"""Ancestry of hull and part draws in the occlusion-cull estimate's extracts (the hull-owner rule's evidence).

Input: the extract JSON of verification/results/occlusion-cull-estimate/extract.py (run14 / run15 captures; local,
untracked). Per draw: the estimate's class from the census body, `ancestry` (object_ancestry count: links from the
draw's node to the top, the node itself included) and whether its parent is null. Output: counts per (class, ancestry,
parent null). Result 2026-10-08 (ancestry.txt): every hull draw with a known ancestry has ancestry 2 (hull -> ship root,
the root top-level), parts 2 or 3, the environment 1 with a null parent: a ship root is a top-level node, so the cull
registers a hull's parent as the owner only when that parent is top-level, and a part's walk ends at the top.
Usage: ancestry.py <run14.json> [<run15.json> ...]
"""
import json
import sys
from collections import Counter

PART_WORDS = ('props', 'turret', 'dock', 'weapon', 'gbarrel', 'antenna', 'effects')
for path in sys.argv[1:]:
    run = json.load(open(path))
    counts = Counter()
    for frame in run['frames'].values():
        census = {}
        for row in frame['census']:
            census.setdefault(row['node'], row)
        for draw in frame['draws'].values():
            body = ((census.get(draw.get('node')) or {}).get('body') or '?').lower()
            ship = body.startswith('ships\\')
            cls = 'part' if ship and any(w in body for w in PART_WORDS) else 'hull' if ship else 'other'
            counts[(cls, draw.get('ancestry'), draw.get('parent') == '00000000')] += 1
    print(path.rsplit('/', 1)[-1])
    for (cls, ancestry, top), n in sorted(counts.items(), key=lambda x: (x[0][0], str(x[0][1]), x[0][2])):
        print(f'  class={cls} ancestry={ancestry} parent_null={int(top)} draws={n}')
