#!/usr/bin/env python3
"""Read-only: records and material params of stations/sadiablo/freighter_base, source (overlay catalogues
skipped, as body_materials.py) or the installed overlay body (--overlay: the full mount, addon/13..15 win).
Usage (repo root): python3 verification/results/outpost-lod-material/material_params.py /mnt/c/X3 [--overlay]"""
import sys
sys.path.insert(0, 'tools/analysis')
from pathlib import Path
import bob1, body_materials
game = Path(sys.argv[1]); overlay = '--overlay' in sys.argv
name = 'stations/sadiablo/freighter_base'
if overlay:
    assets = bob1._archive_modules().Assets(game)
else:
    assets, skipped = body_materials.original_assets(game)
e = bob1.resolve_body(assets, name)
tree = bob1.parse(assets.read_entry(e), None)
print('source', e['source'], e['path'])
mats = bob1.materials(tree)
for k, rec in enumerate(bob1.lods(tree)):
    use = {}
    for p in rec['parts']:
        for g in p['groups']:
            use[g['material']] = use.get(g['material'], 0) + len(g['faces'])
    print('record', k, 'value', rec['value'], 'faces', sum(use.values()), 'mat:faces', sorted(use.items()))
for mi, m in enumerate(mats):
    ps = [(n.decode('latin1'), v) for n, t, v in m.get('params', []) if t != 8]
    tex = [(n.decode('latin1'), v.decode('latin1')) for n, t, v in m.get('params', []) if t == 8]
    print('mat', mi, m.get('effect', b'').decode('latin1'), 'tech', m.get('technique'), 'flags', hex(m.get('flags', 0)))
    print('   num', ps)
    if overlay:
        print('   tex', tex)
