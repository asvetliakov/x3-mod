#!/usr/bin/env python3
"""Bolt tunings 2026-11-08, item #4: the mount tier by the catalogue's `size` column (the 10 % rule) against the old
"any fighter mounts it" rule, and the width w = (3 px x d_eng / 960) x (1 + 0.6 k) each gives (bolt-muzzle-look.md
section 1). Prints every bolt weapon whose tier changed, old and new w (m) and d_eng (m).

  python3 verification/results/bolt-muzzle-look/bolt-tunings-2026-11-08/tier_change.py [catalogue.json ...]
"""
import json
import math
import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parents[4]
D_ENG = {'fighter': 600.0, 'corvette': 1500.0, 'capital': 4000.0}
FIGHTER, CAPITAL = {'M5', 'M4', 'M3'}, {'M1', 'M2', 'M7', 'TL'}
SIZE_TIER = {'small': 'fighter', 'medium': 'corvette', 'big': 'capital'}


def old_tier(used):
    if not used or used & FIGHTER:
        return 'fighter'
    return 'capital' if used <= CAPITAL else 'corvette'


def calibre(w):
    d = max(w.get('damage_shield') or 0, w.get('damage_hull') or 0)
    return min(1.0, max(0.0, math.log10(d / 300) / 2)) if d > 0 else 0.0


def width(tier, rng, k):
    d = D_ENG[tier]
    if rng and rng > 0:
        d = min(d, 0.75 * rng)
    return min(20.0, 3.0 * d / 960.0 * (1 + 0.6 * k)), d


def main(paths):
    for p in paths:
        cat = json.loads(pathlib.Path(p).read_text())
        rows = []
        for w in cat['weapons']:
            if str(w.get('style', '')).startswith('beam'):
                continue
            used = set()
            for laser in w.get('lasers', []):
                used |= set((laser.get('used_by') or {}).keys())
            o, n = old_tier(used), SIZE_TIER.get(w['size'], 'fighter')
            if o == n:
                continue
            k = calibre(w)
            wo, do = width(o, w.get('range_m'), k)
            wn, dn = width(n, w.get('range_m'), k)
            rows.append((w['id'], w['size'], o, n, k, wo, wn, do, dn))
        print(f'{pathlib.Path(p).stem}: {len(rows)} of {len(cat["weapons"])} rows change tier')
        for r in rows:
            print('  %-28s size=%-7s %s -> %s k=%.2f w %.2f -> %.2f m d_eng %.0f -> %.0f m' % r)


if __name__ == '__main__':
    main(sys.argv[1:] or [REPO / 'tools/effects/weapon_catalog/stock_ap.json', REPO / 'tools/effects/weapon_catalog/mayhem3.json'])
