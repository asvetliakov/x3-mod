#!/usr/bin/env python3
"""Read-only: materials of the classic bullet bodies and the muzzle-flash scene bodies in FL (and AP if present).

  python3 -I verification/results/weapon-coverage-census/classic_bullet_bodies.py [--game /mnt/c/X3FL]

Reuses the readers of verification/results/fl-muzzle-flash-draw/fl_muzzle_flash_census.py.
"""
import argparse, importlib.util, pathlib, re, sys
HERE = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('mfc', HERE.parent / 'fl-muzzle-flash-draw/fl_muzzle_flash_census.py')
mfc = importlib.util.module_from_spec(spec); spec.loader.exec_module(mfc)

CLASSIC = [10436, 300, 10458, 10385, 225, 517, 525, 526, 527, 10459, 10678, 518]
FLASH = [445, 444, 446, 447, 450, 314, 10463, 12009, 582]


def flags_text(v):
    try:
        n = int(v, 0)
    except ValueError:
        return v
    return '|'.join(nm for b, nm in mfc.MPF.items() if n & b) + f' {n:#x}'


def report(a, exp, bid):
    hits = [k for k in a.entries if re.fullmatch(r'objects/v/%05d\.(bod|bob)' % bid, k)]
    b = mfc.body(a, bid)
    if b is None:
        print(f'body {bid:05d}: entries {hits} (no text body; binary bob not parsed here)')
        return
    data, _ = mfc.text(a, 'objects/v/%05d.bod' % bid)
    vcol = bool(re.search(r'VERTEXCOLOR|/\s*\(\d+\)\s*;\s*\d+;\s*\d+;\s*\d+', data or '')) if data else None
    mats = []
    for t, i, tex in b['materials']:
        try:
            name, fl, _ = mfc.materials_row(a, exp, tex)
        except Exception as e:  # noqa
            name, fl = '?', '?'
        mats.append(f'MATERIAL{t} #{i} tex {tex} row[12]={name} flags={flags_text(fl)}')
    print(f'body {bid:05d} ({b["src"]}) parts {b["part_names"]}: {b["vertices"]} vertices, {b["uv_faces"]} uv faces, '
          f'UV rect {b["uv_rect"]}; ' + '; '.join(mats))


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--game', default='/mnt/c/X3FL'); o = ap.parse_args()
    a = mfc.mount(o.game, 'fl')
    print('== classic bullet bodies'); [report(a, 'addon2', b) for b in CLASSIC]
    print('== muzzle scene bodies'); [report(a, 'addon2', b) for b in FLASH]


if __name__ == '__main__':
    main()


def raw_materials(game='/mnt/c/X3FL', ids=CLASSIC + FLASH):
    """Print each body's raw MATERIALn header lines (first 160 chars) for the formats body() does not decode."""
    a = mfc.mount(game, 'fl')
    for bid in ids:
        data, _ = mfc.text(a, 'objects/v/%05d.bod' % bid)
        lines = [l.strip()[:160] for l in (data or '').splitlines() if l.strip().startswith('MATERIAL')]
        print(f'{bid:05d}: {len(lines)} material lines; ' + ' || '.join(lines[:4]))
