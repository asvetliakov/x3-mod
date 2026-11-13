#!/usr/bin/env python3
"""Read-only: per classic bullet body of the FL catalogue, its materials' flag words and the blend the lone material
submit gives each (docs/reverse-engineering/fl/bullet-draw-batch.md section 7 "Classic bodies"), with the lone classic
route's verdict (bullet_reshade_core.h lone_blend / classic_verdict).

  python3 -I verification/results/weapon-coverage-census/classic_lone_blends.py [--game /mnt/c/X3FL] \
    > verification/results/weapon-coverage-census/classic_lone_blends_out.txt

The flags are measured (the census readers of classic_bullet_bodies.py); the flag -> blend rule is the classic
submit's: flags & 0x4c7 set -> blended, MPF_DESTINATIONBLEND alone ONE / ONE, MPF_ALPHABLEND SRCALPHA / INVSRCALPHA,
SRCCOLOR SRCCOLOR / INVSRCCOLOR, MULTIPLY(2X) DESTCOLOR / ZERO (SRCCOLOR); none -> opaque, and then the node fade block
(node +0x13c != 0, every live bullet) blends it SRCALPHA / ONE (station-material-distance.md 4c1779.., muzzle-flash-draw.md
section 3, beam-bullet-draw.md section 5; [s] AP code, FL DESTINATIONBLEND re-read, the rest [i] on FL).
"""
import argparse
import importlib.util
import pathlib

HERE = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('cbb', HERE / 'classic_bullet_bodies.py')
cbb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cbb)
mfc = cbb.mfc

ROWS = {10436: 'MASS', 300: 'IOND, TERRANANTIFIGHTER(_1)', 10458: 'PBE, FLAK_ALPHA, FLAK_CLUSTER',
        10385: 'PAL, TRIBEAM, FUSIONBEAM', 225: 'PSG, TERRANANTIFIGHTER_FRAG', 517: 'EMP, EMP_1',
        525: 'TUG, DUMMY2, KH_ALPHA', 526: 'KH_BETA', 527: 'KH_GAMMA', 10459: 'DUMMY1, IPG_FRAG', 10678: 'MINING'}


def word(fl):
    """The flag word from the Materials row (a number, or MPF_ names joined by '|'; MPF_NULL is 0)."""
    try:
        return int(fl, 0)
    except ValueError:
        names = {('MPF_' + n): bit for bit, n in mfc.MPF.items()}
        return sum(names.get(n.strip(), 0) for n in str(fl).split('|'))


def blend(flags):
    if flags & 0x4c7 == 0:
        return 'SRCALPHA/ONE (opaque material, node fade)', 'blend'
    if flags & 0x4:
        return 'SRCALPHA/INVSRCALPHA', 'blend'
    if flags & 0x400:
        return 'SRCCOLOR/INVSRCCOLOR', 'blend'
    if flags & 0xc0:
        return 'DESTCOLOR/ZERO|SRCCOLOR', 'blend'
    if flags & 0x2:
        return 'ONE/ONE', 'admitted (lone_additive)'
    return 'ALPHATEST alone: opaque', 'blend'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', default='/mnt/c/X3FL')
    a = mfc.mount(ap.parse_args().game, 'fl')
    print('body | rows | material | texture | flags | submit blend | lone classic verdict | program')
    for bid, rows in ROWS.items():
        b = mfc.body(a, bid)
        if b is None:
            print(f'v/{bid:05d} | {rows} | text body not parsed')
            continue
        for t, i, tex in b['materials']:
            name, fl, _ = mfc.materials_row(a, 'addon2', tex)
            flags = word(fl)
            text, verdict = blend(flags)
            program = 'standard_lighting2s (0d5848c8)' if flags & 0x10 else 'standard_lighting (ced7edef)'
            print(f'v/{bid:05d} | {rows} | MATERIAL{t} #{i} | {tex} | {cbb.flags_text(fl)} | {text} | {verdict} | {program}')


if __name__ == '__main__':
    main()
