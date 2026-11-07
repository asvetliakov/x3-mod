#!/usr/bin/env python3
"""Hull-impact class per weapon id (docs/architecture/hull-impact-classes.md section 2).

Applies the note's classifier rule to the offline weapon catalogues (tools/effects/weapon_catalog/*.json: TBullets flags,
damage per shot, the vanilla body's elongation) and the note's per-id pins, and prints one row per weapon with the
classifier's class, the pinned class and the class that wins, plus the per-view counts. No game bytes, no Wine.

    python3 verification/results/weapon-effects/hull_class_census.py > verification/results/weapon-effects/hull_class_census_out.txt
"""
import json
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..')
CATALOG = os.path.join(ROOT, 'tools', 'effects', 'weapon_catalog')

# TBullets col 12 flag bits (docs/reference/weapon-catalog.md legend, docs/reverse-engineering/weapon-effect-spawn.md 1).
BEAM = 0x2 | 0x4
HULL_THROUGH_SHIELDS = 0x20
AMMUNITION = 0x40
REPAIR = 0x80
ON_HIT = 0x200 | 0x400  # slow, energy drain: the ion cues
HEAVY_DAMAGE = 20000    # the classifier's heavy band (weapon-effects.md 4.2)


def shape_of(flags, elongation):
    """weapon-effects.md 4.1: beam / wave / tracer / slug / orb."""
    if flags & BEAM:
        return 'beam'
    if flags & 0x8:
        return 'wave'
    if elongation is None:
        return 'slug'
    if elongation >= 4:
        return 'tracer'
    if elongation >= 1.5:
        return 'slug'
    return 'orb'


def classify(flags, damage, elongation):
    """The note's rule, in order; the first match wins."""
    if flags & BEAM and damage == 0:
        return 'tug'
    if flags & REPAIR:
        return 'repair'
    if flags & BEAM:
        return 'beam'
    if flags & ON_HIT:
        return 'ion'
    heavy = damage >= HEAVY_DAMAGE
    if flags & AMMUNITION:
        return 'heavy' if heavy else 'ballistic'
    shape = shape_of(flags, elongation)
    if shape in ('orb', 'wave'):
        return 'plasma'
    if heavy:
        return 'heavy'
    return 'energy'


# The override file's `hull` pins (by_id: any table carrying the id). Stock AP ids first, then Mayhem 3's renamed ids.
PINS = {
    # stock AP: names the flags do not carry
    'SS_BULLET_EMP': 'plasma',              # Electro-Magnetic Plasma Cannon (0x800 only)
    'SS_BULLET_HEPT': 'plasma',             # High Energy Plasma Thrower (a tracer body)
    'SS_BULLET_IPG': 'ion',                 # Ion Pulse Generator (0x1200: no on-hit bit)
    'SS_BULLET_IOND': 'ion',                # Ion Disruptor (0x214: the beam bit would win over its on-hit bit)
    'SS_BULLET_TERANTIFIGHTER': 'plasma',   # Starburst Shockwave Cannon: a bloom, not a railgun
    'SS_BULLET_IBL': 'plasma',              # Incendiary Bomb Launcher: a fire bomb, not a railgun (stock 50,860 per shot)
    'SS_BULLET_PSG': 'plasma',              # Phased Shockwave Generator (stock: a wave body already; Mayhem: a tracer body)
    'SS_BULLET_REPAIR': 'repair',
    'SS_BULLET_TUG': 'tug',
    # Mayhem 3: every flag word is 0 except PBC (0x2) and RL (0xa2) [M, catalogue]
    'SS_BULLET_RL': 'repair',
    'SS_BULLET_MD': 'ballistic',            # Mass Driver
    'SS_BULLET_GC': 'ballistic',            # Gauss Cannon (3,546 per shot: light)
    'SS_BULLET_FBL': 'ballistic',           # Fragmentation Bomb Launcher
    'SS_BULLET_CFA': 'ballistic',           # Cluster Flak Array
    'SS_BULLET_FAA': 'ballistic',           # Flak Artillery Array
    'SS_BULLET_RG': 'heavy',                # Mounted Railgun (an orb body, 25,752 per shot)
    'SS_BULLET_BKE': 'heavy',               # Beta Kyon Emitter (an orb body, 21,451 per shot)
    'SS_BULLET_ID': 'ion',                  # Ion Disruptor
    'SS_BULLET_IC': 'ion',                  # Ion Cannon
    'SS_BULLET_ISR': 'ion',                 # Ion Shard Railgun
    'SS_BULLET_EMPC': 'plasma',             # Electro-Magnetic Plasma Cannon
    'SS_BULLET_PBG': 'plasma',              # Plasma Burst Generator
    'SS_BULLET_SSC': 'plasma',              # Starburst Shockwave Cannon
    'SS_BULLET_PSP': 'plasma',              # Point Singularity Projector (a slug body on Mayhem, 31,364 per shot)
    'SS_BULLET_PALC': 'energy',             # Phased Array Laser Cannon (an orb body)
    'SS_BULLET_PBE': 'energy',              # Pulsed Beam Emitter (an orb body)
    'SS_BULLET_MDS': 'energy',              # Mobile Drilling System (an orb body; asteroids take the victim variant)
}


def main():
    views = ['stock_ap', 'mayhem3', 'fl']
    totals = {}
    for view in views:
        path = os.path.join(CATALOG, view + '.json')
        if not os.path.exists(path):
            continue
        with open(path) as f:
            d = json.load(f)
        print('== %s (%d rows)' % (view, len(d['weapons'])))
        print('%-32s %6s %-6s %7s %-9s %-9s %-9s' % ('id', 'flags', 'shape', 'dmg', 'classifier', 'pin', 'class'))
        counts = {}
        for w in d['weapons']:
            flags = int(w.get('flag_word') or '0', 16) if isinstance(w.get('flag_word'), str) else int(w.get('flag_word') or 0)
            dmg = max(w.get('damage_shield') or 0, w.get('damage_hull') or 0)
            e = w.get('elongation')
            c = classify(flags, dmg, e)
            pin = PINS.get(w['id'], '')
            final = pin or c
            counts[final] = counts.get(final, 0) + 1
            print('%-32s %6s %-6s %7d %-9s %-9s %-9s' % (w['id'], '0x%x' % flags, shape_of(flags, e), dmg, c, pin or '-', final))
        print('counts:', ' '.join('%s=%d' % kv for kv in sorted(counts.items())))
        print('pins used: %d of %d rows' % (sum(1 for w in d['weapons'] if w['id'] in PINS), len(d['weapons'])))
        totals[view] = counts
        print()
    unused = sorted(k for k in PINS if not any(k in [w['id'] for w in json.load(open(os.path.join(CATALOG, v + '.json')))['weapons']]
                                               for v in views if os.path.exists(os.path.join(CATALOG, v + '.json'))))
    print('pins naming no catalogue id:', unused or 'none')
    return 0


if __name__ == '__main__':
    sys.exit(main())
