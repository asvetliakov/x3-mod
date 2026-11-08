#!/usr/bin/env python3
"""Bolt tunings 2026-11-08, item #12: the lab's texture means (assets.js `mean`, tools/effects/weapon_effects_lab_assets.py)
against the production table's texture_rgb (tools/effects/weapon_effects_table.py), weapon by weapon, and the table's
mount tiers and bolt pins.

  python3 tools/effects/weapon_effects_table.py --game-root /mnt/c/X3 --stock-only --out <dir>/table_stock.json
  python3 tools/effects/weapon_effects_table.py --game-root /mnt/c/X3 --out <dir>/table_mayhem3.json
  python3 verification/results/bolt-muzzle-look/bolt-tunings-2026-11-08/tint_check.py <dir>
"""
import hashlib
import json
import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parents[4]
LAB = REPO / 'verification/results/weapon-effects-lab'
SHOW = ('SS_BULLET_FLAK_ALPHA', 'SS_BULLET_CIG', 'SS_BULLET_MAM', 'SS_BULLET_PAC', 'SS_BULLET_PSG', 'SS_BULLET_MD')


def assets(view):
    text = (LAB / view / 'assets.js').read_text()
    start = text.index('] = ') + 4
    return json.loads(text[start:text.rindex(';')])


def main(d):
    d = pathlib.Path(d)
    for view, name in (('stock_ap', 'table_stock.json'), ('mayhem3', 'table_mayhem3.json')):
        raw = (d / name).read_bytes()
        table = json.loads(raw)
        bodies = assets(view)['bodies']
        same = diff = missing = 0
        for e in table['tables']['bullets']['entries']:
            lab = (bodies.get(e['body']) or {}).get('mean') if e['body'] else None
            if e['texture_rgb'] is None and lab is None:
                continue
            if lab is None or e['texture_rgb'] is None:
                missing += 1
            elif lab == e['texture_rgb']:
                same += 1
            else:
                diff += 1
                print(f'  DIFF {view} {e["id"]} table={e["texture_rgb"]} lab={lab}')
            if e['id'] in SHOW:
                print(f'  {view} {e["id"]}: table texture_rgb={e["texture_rgb"]} lab mean={lab} tier={e["mount_tier"]}')
        pins = {e['id']: {k: e[k] for k in ('style', 'bolt_e_body', 'bolt_w_orb', 'bolt_halo', 'bolt_tint') if e.get(k) is not None}
                for e in table['tables']['bullets']['entries']
                if any(k in e for k in ('bolt_e_body', 'bolt_w_orb', 'bolt_halo', 'bolt_tint'))}
        print(f'{view}: sha256={hashlib.sha256(raw).hexdigest()} tbullets={table["tables"]["bullets"]["sha256"]} '
              f'counts={ {k: table["counts"][k] for k in ("fighter", "corvette", "capital", "overrides")} } '
              f'means same={same} differ={diff} one-sided={missing} pins={pins}')


if __name__ == '__main__':
    main(sys.argv[1])
