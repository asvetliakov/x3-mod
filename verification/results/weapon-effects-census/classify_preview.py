#!/usr/bin/env python3
"""Preview of the weapon-effects classifier of docs/architecture/weapon-effects.md section 3 over the census.

Reads verification/results/weapon-effects-census/<view>.json (flag word, speed, lifetime, quad size, light colour,
the vanilla body's elongation z_over_xy) and, for the calibre, the census fields damage_shield (TBullets col 7) and
damage_hull (col 22). Prints one
markdown table per view (--view, default all) with the style, calibre band and tint source the rules give, and the
per-style counts. Only numbers and ids are printed; no game bytes are written.
Usage: python3 verification/results/weapon-effects-census/classify_preview.py [--view stock_ap|mayhem3|fl] [--counts]
"""
import argparse
import json
import math
import pathlib


HERE = pathlib.Path(__file__).resolve().parent
# Section 3 thresholds.
ELONGATION_TRACER, ELONGATION_SLUG = 4.0, 1.5     # z_over_xy of the vanilla body's LOD-0 half-extents
CALIBRE_LIGHT, CALIBRE_HEAVY = 3000, 20000          # max(col 7, col 22) damage per shot
CALIBRE_ZERO, CALIBRE_ONE = 300.0, 30000.0          # k = clamp(log10(d / 300) / 2, 0, 1)
LIGHT_SAT, LIGHT_MIN = 0.25, 64                     # light colour (cols 13-15): (max - min) / max, max


def style(flags, elongation):
    """shape[+burst]: the shape from the beam bits, the fixed-length bit, else the body's elongation; +burst when
    the bullet ends in its own death explosion (0x100) or fragments (0x1000). 0x2000 (sustained, col 27) is a
    parameter, not a style."""
    if flags & 0x6:          # 0x2 gun-tied, 0x4 kind B
        shape = 'beam'
    elif flags & 0x8:
        shape = 'wave'
    elif elongation is None:
        shape = 'slug'
    elif elongation >= ELONGATION_TRACER:
        shape = 'tracer'
    elif elongation >= ELONGATION_SLUG:
        shape = 'slug'
    else:
        shape = 'orb'
    return shape + ('+burst' if flags & 0x1100 else '')


def calibre(shield, hull):
    d = max(shield, hull, 0)
    band = 'light' if d < CALIBRE_LIGHT else 'heavy' if d >= CALIBRE_HEAVY else 'medium'
    k = 0.0 if d <= 0 else min(1.0, max(0.0, math.log10(d / CALIBRE_ZERO) / math.log10(CALIBRE_ONE / CALIBRE_ZERO)))
    return band, k


def tint_source(light):
    hi, lo = max(light), min(light)
    if hi >= LIGHT_MIN and (hi - lo) / hi >= LIGHT_SAT:
        return 'light'
    return 'body_texture'


def table(view, counts_only=False):
    d = json.loads((HERE / f'{view}.json').read_text())
    rows, tally = [], {}
    for w in d['weapons']:
        ext = d['bodies'].get(w['body_key'], {}).get('extents') or {}
        e = ext.get('z_over_xy')
        s = style(w['flag_word'], e)
        band, k = calibre(w['damage_shield'], w['damage_hull'])
        t = tint_source(w['light_colour'])
        tally[s] = tally.get(s, 0) + 1
        rows.append(f"| {w['weapon_id'].removeprefix('SS_BULLET_')} | {w['flag_word']:#x} | "
                    f"{e if e is not None else '-'} | {w['speed'] // 1000} | {w['lifetime_ms']} | "
                    f"{w['damage_per_shot']} | {s} | {band} {k:.2f} | {t} |")
    print(f'## {view}: {len(rows)} rows; styles ' + ', '.join(f'{k} {v}' for k, v in sorted(tally.items())))
    if not counts_only:
        print('| weapon | flags | z/xy | m/s | life ms | dmg/shot | style | calibre | tint |')
        print('| --- | --- | --- | --- | --- | --- | --- | --- | --- |')
        print('\n'.join(rows))


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--view', choices=('stock_ap', 'mayhem3', 'fl'))
    p.add_argument('--counts', action='store_true')
    a = p.parse_args()
    for v in ([a.view] if a.view else ('stock_ap', 'mayhem3', 'fl')):
        table(v, a.counts)


if __name__ == '__main__':
    main()
