"""Stray bytes after /BOB over every winning binary body of the install (read-only), as seen by
fog_families (sector_fog_census.Assets, every catalogue) and by body_materials / atlas_census
(the original assets without the LOD overlay catalogues). Split: nebula dust parts, nebula
background bodies, other nebula bodies, everything else. A body that does not parse even with an
unbounded tail is counted as parse_error.

PYTHONPATH=verification/probe:tools/analysis python3 verification/results/lod-mayhem-refusals/tail_census.py
"""
import sys
from collections import Counter
from pathlib import Path

import bob1
import lod_batch_census
import lod_overlay
import sector_fog_census as sfc

GAME = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'


def group(key):
    k = key.lower()
    if '/environments/nebulae/' in k:
        return 'nebula_dust' if '_dust_part' in k else 'nebula_background' if '_background_' in k else 'nebula_other'
    return 'other'


def census(assets, label):
    counts, tails, errors = Counter(), Counter(), Counter()
    rows = []
    for key in lod_batch_census.body_keys(assets):
        entry = assets.entries[key][-1]
        data = assets.read_entry(entry)
        assets.cache.clear()
        if bob1.kind(data) != 'BOB1':
            continue
        g = group(key)
        counts[g] += 1
        try:
            n = bob1.parse(data, None).get('trailing_bytes', 0)
        except bob1.FormatError:
            errors[g] += 1
            continue
        if n:
            tails[g] += 1
            if g != 'other':
                rows.append(f'  {key} {n}')
    dust = Counter(v[-1]['path'].rsplit('.', 1)[-1].lower() for k, v in assets.entries.items()
                   if '/environments/nebulae/' in k.lower() and '_dust_part' in k.lower())
    print(f'{label}: BOB1 bodies {dict(counts)}; with a tail {dict(tails)}; parse_error {dict(errors)};'
          f' winning nebula dust members by extension {dict(dust)}')
    for r in rows:
        print(r)


def main():
    census(sfc.Assets(GAME), 'fog_families view (all catalogues)')
    census(lod_overlay.original_assets(GAME)[0], 'body_materials/atlas_census view (overlay catalogues left out)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
