#!/usr/bin/env python3
"""Beam-class tint resolution per TBullets row (the review's script, 2026-11-07; docs/verification/weapon-effects.md
"Beam colour and ion lightning").

The DLL's classifier (weapon_effects_core.h: the override tint, else the light colour when saturated, else the body
texture's mean, else white), tint_linear (sRGB to linear, normalised to its maximum), the spot's halo / core mixes
(hull::beam_glow_mix 0.6, beam_core_mix 0.3) and the colour gate (stage::beam_coloured: a light or texture source whose
8-bit saturation is above hull::beam_colour_sat_min 0.15; else the heat look), per beam-flagged weapon (flags 0x2 / 0x4)
of the offline catalogues tools/effects/weapon_catalog/{stock_ap,mayhem3}.json against generated tables (no game bytes):

    python3 tools/effects/weapon_effects_table.py --game-root <game> --stock-only --edition ap --out <scratch>/stock.json
    python3 tools/effects/weapon_effects_table.py --game-root <game> --edition ap --out <scratch>/mayhem.json
    python3 -I verification/results/weapon-effects-ion-lightning/beam_tint.py <scratch>/stock.json <scratch>/mayhem.json
"""
import json
import os
import sys

W = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'tools', 'effects')
SAT_MIN = 0.15


def srgb(v):
    v /= 255.0
    return v / 12.92 if v <= 0.04045 else (v * v) * (0.7 + 0.3 * v)


def lin(rgb):
    o = [srgb(c) for c in rgb]
    m = max(o)
    return [c / m if m > 0 else 1.0 for c in o]


def sat_light(r, g, b):
    hi, lo = max(r, g, b), max(0, min(r, g, b))
    return hi >= 64 and 4 * (hi - lo) >= hi


def satur(c):
    return (max(c) - min(c)) / max(c) if max(c) > 0 else 0


for name, cat, table in (('stock_ap', 'stock_ap.json', sys.argv[1]), ('mayhem3', 'mayhem3.json', sys.argv[2])):
    c = json.load(open(os.path.join(W, 'weapon_catalog', cat)))
    t = json.load(open(table))['tables']['bullets']
    rows = {e['row']: e for e in t['entries']}
    print('==', name, t['sha256'][:16])
    for w in c['weapons']:
        fw = int(w['flag_word'], 16)
        if not fw & 0x6:
            continue
        e = rows.get(w['bullet_index'], {})
        hull = e.get('hull')
        if hull in ('energy', 'ballistic', 'plasma', 'heavy', 'ion'):
            cls = 'pinned:' + hull
        elif fw & 0x6 and w['damage_hull'] <= 0 and w['damage_shield'] <= 0:
            cls = 'tug'
        elif hull == 'tug':
            cls = 'tug'
        elif fw & 0x80 or hull == 'repair':
            cls = 'repair'
        else:
            cls = 'beam'
        L = w['light_rgb']
        tex = e.get('texture_rgb')
        ov = e.get('tint')
        if ov:
            src, rgb = 'override', ov
        elif sat_light(*L):
            src, rgb = 'light', L
        elif tex:
            src, rgb = 'texture', tex
        else:
            src, rgb = 'white', [255, 255, 255]
        coloured = cls == 'beam' and src in ('light', 'texture') and max(rgb) > 0 and \
            max(rgb) - min(rgb) > SAT_MIN * max(rgb)
        tint = lin(rgb)
        halo = [1 + (x - 1) * 0.6 for x in tint]
        core = [1 + (x - 1) * 0.3 for x in tint]
        names = ','.join(l['name'] for l in w['lasers'])[:40]
        look = ('colour' if coloured else 'heat') if cls == 'beam' else '-'
        print(f"{w['id']:<26} {names:<40} cls={cls:<10} light={L} tex={tex} src={src:<8} "
              f"tint=({tint[0]:.2f},{tint[1]:.2f},{tint[2]:.2f}) halo=({halo[0]:.2f},{halo[1]:.2f},{halo[2]:.2f}) "
              f"sat={satur(halo):.2f} core_sat={satur(core):.2f} spot={look}")
