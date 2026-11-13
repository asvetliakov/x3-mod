#!/usr/bin/env python3
"""The bullet re-shade's table coverage on AP (docs/verification/weapon-effects.md, "Re-shade on AP"): for each
generated weapon_effects.json (tools/effects/weapon_effects_table.py --edition ap [--stock-only]) whether every TBullets
row's body has a tables.bullet_bodies entry (the body map, so the geometric size) and every row's texture a
tables.bullet_textures entry (the per-texture normaliser; a missing one keeps the IRE's 2.75), and how many rows draw
an effects-material body (the batch / lone path) against a classic one (the lone classic path).

  python3 -I verification/results/ap-reshade/table_coverage.py <mayhem.json> <stock.json> \
    > verification/results/ap-reshade/table_coverage_out.txt
Reads the generated tables only (they stay untracked); prints counts.
"""
import json
import sys


def coverage(path):
    d = json.load(open(path))
    t = d['tables']
    rows = t['bullets']['entries']
    bodies = {b['body'].lower() for b in t['bullet_bodies']}
    covered_rows = set()
    for x in t['bullet_textures']:
        covered_rows.update(x['rows'])
    row_bodies = {r['body'].lower() for r in rows if r.get('body')}
    row_textures = {r['texture'].lower() for r in rows if r.get('texture')}
    textured = [r for r in rows if r.get('texture')]
    print(f'{path.rsplit("/", 1)[-1]}: tbullets {d["counts"].get("bullets")} rows, {len(row_bodies)} distinct bodies, '
          f'{len(bodies)} bullet_bodies entries, bodies without an entry {sorted(row_bodies - bodies)}; '
          f'{len(row_textures)} distinct row textures, {len(t["bullet_textures"])} bullet_textures entries, '
          f'{len(textured)} rows with a texture, textured rows without an entry '
          f'{sorted(r["row"] for r in textured if r["row"] not in covered_rows)}, rows without a texture '
          f'{[r["row"] for r in rows if not r.get("texture")]}')
    for r in textured:
        if r['row'] not in covered_rows:
            print(f'  row {r["row"]} {r["id"]} body {r["body"]} texture {r["texture"]}')
    for r in rows:
        if not r.get('texture'):
            print(f'  untextured row {r["row"]} {r["id"]} body {r["body"]} shape {r.get("shape")}')


if __name__ == '__main__':
    for p in sys.argv[1:]:
        coverage(p)
