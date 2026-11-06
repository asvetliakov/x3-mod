#!/usr/bin/env python3
"""Vanilla effect timings and beam envelopes behind docs/architecture/weapon-look.md.

Reads only the tracked weapon catalogue (tools/effects/weapon_catalog/*.json) and the weapon-effects census
(verification/results/weapon-effects-census/stock_ap.json, local). Prints compact rows; run with python3 -I.
"""
import json, os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
CAT = os.path.join(ROOT, 'tools', 'effects', 'weapon_catalog')
CENSUS = os.path.join(ROOT, 'verification', 'results', 'weapon-effects-census', 'stock_ap.json')


def pct(xs, q):
    return xs[min(len(xs) - 1, int(q * (len(xs) - 1) + 0.5))] if xs else None


for view in ('stock_ap', 'mayhem3', 'fl'):
    weapons = json.load(open(os.path.join(CAT, view + '.json')))['weapons']
    for role in ('muzzle', 'impact', 'death', 'flight'):
        longest, lit = [], 0
        for w in weapons:
            e = w['effects'].get(role)
            if not e:
                continue
            t = [int(x) for x in re.findall(r'(\d+) ms', e['summary'])]
            if t:
                longest.append(max(t))
            lit += 'light' in e['summary']
        longest.sort()
        print('timing view=%s role=%s rows=%d longest_ms p10=%s p50=%s p90=%s max=%s with_light=%d' % (
            view, role, len(longest), pct(longest, .1), pct(longest, .5), pct(longest, .9),
            longest[-1] if longest else None, lit))
    for w in weapons:
        if w['style'] != 'beam':
            continue
        refire = min(l['refire_ms'] for l in w['lasers']) if w['lasers'] else None
        kind = '-' if refire is None else ('sustained' if w['lifetime_ms'] >= refire else 'pulsed')
        print('beam view=%s id=%s flags=%s life_ms=%d refire_ms=%s envelope=%s' % (
            view, w['id'].removeprefix('SS_BULLET_'), w['flag_word'], w['lifetime_ms'], refire, kind))
    speeds = sorted(w['speed_mps'] for w in weapons if w['style'] != 'beam' and w['speed_mps'] > 0)
    print('bolt_speed view=%s n=%d mps p10=%s p50=%s p90=%s' % (view, len(speeds), pct(speeds, .1), pct(speeds, .5), pct(speeds, .9)))

if not os.path.exists(CENSUS):
    sys.exit(0)
effects = json.load(open(CENSUS))['effects']
for row in ('35', '40', '37', '201', '48', '59', '39', '158', '155', '189', '196'):
    r = effects.get(row)
    if not r:
        print('death_row %s missing' % row)
        continue
    els = r.get('elements', [])
    ends = [el['duration'] + el.get('timeoffset', 0) for el in els if 'duration' in el]
    props = [el for el in els if 'prop_effect_id' in el]
    print('row %s flags=%s elements=%d last_end_ms=%s propagate=%s cut_scenes=%d' % (
        row, r.get('effectflags'), len(els), max(ends) if ends else None,
        ['%s x%s' % (p['prop_effect_id'], p['max_props']) for p in props],
        sum(1 for el in els if 'EEDF_CUT' in el.get('flag_names', []) and 'scene' in str(el.get('body_key')))))
