#!/usr/bin/env python3
"""The bullet re-shade's geometric size inputs (docs/architecture/projectile-look.md "Geometric size (2026-11-10)"):
tables.bullet_bodies of the table tools/effects/weapon_effects_table.py generates for an installation (uv_layout per
body: the head group's UV centre and half-side r, the fin group's axis v, rim v and u range, or why a group is absent),
with the rows that draw each body. Prints numbers only; writes nothing.

  python3 -I verification/results/projectile-reshade/bullet_bodies.py [--game-root /mnt/c/X3FL] [--edition fl] \
    > verification/results/projectile-reshade/bullet_bodies_out.txt
"""
import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/effects'))
import weapon_effects_table as wt  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game-root', default='/mnt/c/X3FL')
    ap.add_argument('--edition', default='fl')
    args = ap.parse_args()
    table = wt.generate(args.game_root, edition=args.edition)
    bodies = table['tables']['bullet_bodies']
    rows = {}
    for e in table['tables']['bullets']['entries']:
        rows.setdefault(e['body'], []).append(e['id'])
    print(f"tbullets {table['tables']['bullets']['sha256']} rows {table['tables']['bullets']['rows']}; {len(bodies)} bodies, "
          f"{sum(1 for b in bodies if b['head'])} with a head, {sum(1 for b in bodies if b['fin'])} with fins")
    print('body | groups | head u, v, r | fin axis v, rim v, u0..u1 | head reason | fin reason | rows')
    for b in bodies:
        h, f = b['head'], b['fin']
        head = f"{h['u']}, {h['v']}, {h['r']}" if h else '-'
        fin = f"{f['axis_v']}, {f['rim_v']}, {f['u0']}..{f['u1']}" if f else '-'
        print(f"{b['body']} | {b.get('groups')} | {head} | {fin} | {b.get('head_reason')} | {b.get('fin_reason')} | "
              f"{','.join(rows.get(b['body'], []))}")


if __name__ == '__main__':
    main()
