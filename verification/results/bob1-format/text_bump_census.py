#!/usr/bin/env python3
"""Winning text bodies of a game root whose materials carry t_BumpTexture (effect parameter
semantic id 2 at 0x004ba500; a STRING value is resolved to a LONG texture id by 0x00470320
when the texture exists -> material flag 0x2000 at 0x00481780 -> model +0x50 & 4 -> tangent declaration and
the BUMPMAP technique, drawn with zero tangents from a text load; body-text-loader.md). Counts only.

  PYTHONPATH=tools/analysis python3 verification/results/bob1-format/text_bump_census.py [GAME]
"""
import collections
import sys
from pathlib import Path

import bob1
import sector_fog_census as sfc

game = Path(sys.argv[1]) if len(sys.argv) > 1 else bob1.DEFAULT_GAME
a = sfc.Assets(game)
keys = sorted(k for k, v in a.entries.items() if v[-1]['path'].lower().endswith(('.pbd', '.bod')))
C = collections.Counter()
for k in keys:
    d = a.read_entry(a.entries[k][-1]); a.cache.clear()
    if bob1.text_kind(d) == 'scene':
        continue
    C['text_bodies'] += 1
    try:
        tree = bob1.parse_text(d)
    except bob1.FormatError:
        C['refused'] += 1
        continue
    bump = False
    for m in bob1.materials(tree):
        for name, typ, val in m.get('params', []):
            if name.lower() == b't_bumptexture':
                C[f'bump_param_type_{typ}'] += 1
                bump = bump or (bool(val) if typ == 8 else val[0] != 0)
        if 'params' not in m and any(mp for mp, _ in m.get('maps', [])[1:2]):
            C['classic_second_map'] += 1
            bump = True
    C['bodies_with_bump'] += bump
for k in sorted(C):
    print(k, C[k])
