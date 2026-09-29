#!/usr/bin/env python3
"""Read-only: TBackgrounds rows with NumDustInstances > 0 and whether their dust bodies exist.
Usage: PYTHONPATH=tools/analysis python3 verification/results/run383-dust-leak/dust_bodies.py <game root> [--stock]
Prints one line per background with dust > 0: index, family, dust count, rates, and the dust parts
(1-based, rate > 0) whose body objects/environments/nebulae/<fam>/nebula_<fam>_dust_partNN.{pbd,bod,pbb,bob}
is missing. Derived metadata only; no asset bytes are written."""
import sys
from pathlib import Path
import sector_fog_census as sfc

root = Path(sys.argv[1])
assets = sfc.Assets(root, catalogues=sfc.STOCK_AP_CATALOGUES if '--stock' in sys.argv else None)
data, prov = assets.get('types/TBackgrounds.txt') if assets.candidates('types/TBackgrounds.txt') else assets.get('types/TBackgrounds.pck')
rows = sfc.backgrounds(data)
print('source', prov['source'], prov['member'], 'rows', len(rows))
leaky = 0
for r in rows:
    if r['dust'] <= 0:
        continue
    fam = r['family']
    missing, present = [], []
    for i, rate in enumerate(r['body_rates']):
        if rate <= 0:
            continue
        stem = f'objects/environments/nebulae/{fam}/nebula_{fam}_dust_part{i + 1:02d}'
        found = any(assets.candidates(stem + ext) for ext in ('.pbd', '.bod', '.pbb', '.bob'))
        (present if found else missing).append(i + 1)
    total = sum(r['body_rates'])
    all_fail = total <= 0 or not present
    leaky += all_fail
    print(f"bg {r['index']:3d} {fam:20s} dust={r['dust']:3d} rate_sum={total:4d} present={present} missing={missing}"
          f" all_picks_fail={int(all_fail)}")
print('backgrounds_with_dust_and_all_picks_failing', leaky)
