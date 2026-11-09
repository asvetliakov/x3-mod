#!/usr/bin/env python3
"""Crop the dump's ire-muzzle frame (1920x1080, verification/probe/weapon_effects_projectile_dump.cpp through
`run_weapon_effects.py --host native --dump DIR`) to the fighter and its two mounts: 800 x 450 px from (560, 600).

  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_weapon_effects.py \
      --host native --dump <scratch>
  python3 verification/results/muzzle-flash-look/crop_fixture.py <scratch>/ire-muzzle.png

writes fixture-ire-muzzle.png beside this script; the uncropped frame and the PFMs stay local and are deleted.
"""
import sys
from pathlib import Path

from PIL import Image

BOX = (560, 600, 1360, 1050)


def main():
    source = Path(sys.argv[1])
    out = Path(__file__).resolve().parent / 'fixture-ire-muzzle.png'
    with Image.open(source) as img:
        if img.size != (1920, 1080):
            raise SystemExit(f'{source}: {img.size}, expected the dump frame 1920x1080')
        img.crop(BOX).save(out, optimize=True)
    print(out)


if __name__ == '__main__':
    main()
