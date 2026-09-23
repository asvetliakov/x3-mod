#!/usr/bin/env python3
"""Max |position| per LOD record (BOB units) over the winning bodies of a game root, binary (.pbb/.bob)
or text (.pbd/.bod, compiled by bob1.parse_text), by top directory: is every record normalised to
65535/65536? Read-only.

  PYTHONPATH=tools/analysis python3 record_extent.py GAME bin|text
"""
import collections
import sys
from pathlib import Path

import bob1
import sector_fog_census as sfc

a = sfc.Assets(Path(sys.argv[1]))
mode = sys.argv[2]
c = collections.Counter()
ex = []
for k, v in sorted(a.entries.items()):
    e = v[-1]
    p = e['path'].lower()
    if not p.endswith(('.pbd', '.bod') if mode == 'text' else ('.pbb', '.bob')):
        continue
    d = a.read_entry(e)
    a.cache.clear()
    if mode == 'bin' and bob1.kind(d) != 'BOB1':
        continue
    if mode == 'text' and bob1.text_kind(d) == 'scene':
        continue
    try:
        t = bob1.parse(d, 8)
    except bob1.FormatError:
        continue
    for li, lod in enumerate(bob1.lods(t)):
        m = max((max(abs(q) for q in pt[1:4]) for pt in lod['points']), default=0)
        cls = str(m) if m in (65535, 65536) else 'other'
        c[(k.split('/')[1] if k.startswith('objects') else 'addon', cls)] += 1
        if cls == 'other' and len(ex) < 10:
            ex.append((k, li, m))
print('records by (top directory, max |position|)', sorted(c.items()))
print('records', sum(c.values()), 'not at 65535/65536', sum(n for (_, cls), n in c.items() if cls == 'other'))
print('examples', ex)
