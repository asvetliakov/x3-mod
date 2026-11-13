#!/usr/bin/env python3
"""Read-only: per TBullets row of the FL catalogue, body, flag word, beam bits, muzzle Effects row, lasers.

  python3 -I verification/results/weapon-coverage-census/bullet_coverage_table.py
Input: tools/effects/weapon_catalog/fl.json (TLaser col 9 -> TBullets; TBullets col 0 body, col 12 flags, col 21 muzzle).
"""
import json, pathlib
d = json.loads((pathlib.Path(__file__).resolve().parents[3] / 'tools/effects/weapon_catalog/fl.json').read_text())
for w in sorted(d['weapons'], key=lambda w: w['bullet_index']):
    f = int(w['flag_word'], 16)
    beam = [n for b, n in ((2, 'A'), (4, 'B'), (8, 'fixed'), (0x2000, 'cont')) if f & b]
    m = (w['effects']['muzzle'] or {}).get('row')
    print(w['bullet_index'], w['id'], w['body'], w['flag_word'], '+'.join(beam) or '-', 'muzzle', m, '|',
          ', '.join(l['name'] or l['id'] for l in w['lasers']))
