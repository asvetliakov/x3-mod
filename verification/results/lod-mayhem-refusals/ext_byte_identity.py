#!/usr/bin/env python3
"""Acceptance 4 of the extension-precedence change: bake a fixed set of bodies that the installed bake
record lists as built (census row, then lod_overlay.bake_body with the record's atlas settings) and print
the sha256 of every member the bake writes. Run once with PYTHONPATH on the pre-change tools
(git archive HEAD tools/analysis) and once on the working tree; the outputs must be equal.
Read-only over the game tree. Usage:
  PYTHONPATH=<tools/analysis of either tree> python3 ext_byte_identity.py [game_root] > OUT"""
import hashlib
import json
import sys
from pathlib import Path

import lod_batch_census as census
import lod_overlay

GAME = Path(sys.argv[1] if len(sys.argv) > 1 else
            Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3')
record = json.load(open(GAME / 'addon' / 'x3m-lod-batch.json'))
built = sorted((b for b in record['bodies'] if b.get('eligible') and b['cat'] in ('ship', 'station')),
               key=lambda b: b['name'].lower())
names = []
for ext in ('.pbb', '.bob', '.pbd'):                       # two of each member form the record built
    for cat in ('ship', 'station'):
        names += [b['name'] for b in built if b['member'].lower().endswith(ext) and b['cat'] == cat][:1]
s = record['settings']
atlas_opts = dict(s['atlas'], sizes=tuple(s['atlas']['sizes']))
opts = dict(sizes=atlas_opts['sizes'], include_other=False, widths=(s['screen_width'],),
            texel=dict(min_texels=atlas_opts['min_texels'], floor_share=atlas_opts['texel_floor_share'],
                       fallback=atlas_opts['texel_fallback']),
            rule=dict(census.RULE, aspect=s['rule']['aspect'], aspect_ship=s['rule']['aspect_ship'],
                      aspect_station=s['rule']['aspect_station']))
rows, _ = census.run(GAME, opts, only={census.body_key(n) for n in names}, include_text=True)
assets, _ = lod_overlay.original_assets(GAME)
for row in sorted(rows, key=lambda r: r['name'].lower()):
    res = lod_overlay.bake_body(assets, row, atlas_opts)
    if 'refused' in res:
        print(f'{row["name"]} refused {res["refused"][:120]}')
        continue
    print(f'{row["name"]} source={res["source"]} member={res["member"]} members={len(res["members"])}')
    for m, data in res['members']:
        print(f'  {m} {len(data)} {hashlib.sha256(data).hexdigest()}')
