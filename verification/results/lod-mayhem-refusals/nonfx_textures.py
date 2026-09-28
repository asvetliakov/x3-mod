#!/usr/bin/env python3
"""Q2 supplement: which file (or placeholder) the engine binds for every texture name of the classic
materials in the non_effect_material ship/station bodies (lod_atlas.lookup = the 0x004f4cb0 /
0x004f3510 rule of texture-lookup.md). Slots: texture (diffuse), map0 (+0x2e), map1 (+0x32 bump),
map2 (+0x36 light), extra0 (+0x1c specular), extra1 (+0x20). Read-only.
  PYTHONPATH=tools/analysis python3 nonfx_textures.py > nonfx_textures_out.txt"""
import collections
import json
from pathlib import Path

import bob1
import lod_atlas
import sector_fog_census as sfc

GAME = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
rec = json.load(open(GAME / 'addon' / 'x3m-lod-batch.json'))
bodies = [b for b in rec['bodies'] if 'non_effect_material' in (b.get('refuse') or []) and b['cat'] != 'other']
assets = sfc.Assets(GAME)
by_member = {f'{e["source"]}:{e["path"]}'.lower(): e for lst in assets.entries.values() for e in lst}
res = collections.Counter()
for b in bodies:
    tree = bob1.parse(assets.read_entry(by_member[b['member'].lower()]), max_trailing=16)
    for m in bob1.materials(tree):
        if 'params' in m or not isinstance(m['texture'], bytes):
            continue
        slots = [('texture', m['texture'])] + [(f'map{i}', n) for i, (n, _) in enumerate(m['maps'])] + \
                [(f'extra{i}', n) for i, (n, _) in enumerate(m.get('extra', []))]
        for slot, name in slots:
            if name in (b'', b'NULL'):
                continue
            try:
                r = lod_atlas.lookup(assets, name)
            except Exception as ex:                       # report, do not hide
                r = ('error', type(ex).__name__)
            if r is None:
                kind = 'no texture'
            elif r[0] == 'error':
                kind = 'error ' + r[1]
            elif r[1]:
                kind = 'placeholder ' + r[1]
            else:
                kind = 'file ' + r[0]['source']
            res[(b['member'].split(':')[0], slot, kind)] += 1
for k, n in sorted(res.items()):
    print(f'{k[0]:13} {k[1]:7} {k[2]:40} {n}')
