"""table_mount_check.py <weapon_effects.json> <catalogue.json>: the generated table's mount_tier and range_m per bullet
row against the tracked catalogue's used-by rule and range (bolt-muzzle-look.md section 7, bolt_width_law's last
clause). Prints the schema, the tier counts and the mismatching rows; exit 1 on any mismatch."""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/effects'))
import weapon_effects_table as wt  # noqa: E402

table, catalogue = (json.loads(Path(p).read_text()) for p in sys.argv[1:3])
rows = {x['row']: x for x in table['tables']['bullets']['entries']}
bad = 0
for w in catalogue['weapons']:
    u = set()
    for laser in w['lasers']:
        u |= set((laser.get('used_by') or {}).keys())
    row = rows[w['bullet_index']]
    if row['mount_tier'] != wt.mount_tier_of(u) or (row['range_m'] or 0) != w['range_m']:
        bad += 1
        print('mismatch', w['id'], row['mount_tier'], wt.mount_tier_of(u), row['range_m'], w['range_m'])
print('schema', table['schema'], {t: table['counts'][t] for t in wt.MOUNT_TIERS}, 'mismatches', bad, 'of',
      len(catalogue['weapons']))
sys.exit(1 if bad else 0)
