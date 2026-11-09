#!/usr/bin/env python3
"""The bullet re-shade's per-texture normaliser inputs (docs/reverse-engineering/fl/bullet-draw-batch.md section 6):
tables.bullet_textures of the table tools/effects/weapon_effects_table.py generates for an installation, with the c31.w
each entry gets (bullet_reshade_core.h texture_scale: 2.75 x 0.6169 / p99). Prints numbers only; writes nothing.

  python3 -I verification/results/projectile-reshade/bullet_textures.py [--game-root /mnt/c/X3FL] [--edition fl] \
    > verification/results/projectile-reshade/bullet_textures_out.txt
"""
import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/effects'))
import weapon_effects_table as wt  # noqa: E402

LUMINANCE_REFERENCE = 2.75 * 0.6169  # bullet_reshade_core.h luminance_scale x ire_texture_p99


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game-root', default='/mnt/c/X3FL')
    ap.add_argument('--edition', default='fl')
    args = ap.parse_args()
    table = wt.generate(args.game_root, edition=args.edition)
    textures = table['tables']['bullet_textures']
    print(f"tbullets {table['tables']['bullets']['sha256']} rows {table['tables']['bullets']['rows']}; "
          f"{len(textures)} bullet texture frames")
    print('name | member | bytes | head64 | fnv1a64 | peak | p99 | c31.w | rows')
    for t in textures:
        print(f"{t['name']} | {t['member']} | {t['bytes']} | {t['head64']} | {t['fnv1a64']} | {t['peak']} | {t['p99']} | "
              f"{LUMINANCE_REFERENCE / t['p99']:.3f} | {','.join(str(r) for r in t['rows'])}")


if __name__ == '__main__':
    main()
