#!/usr/bin/env python3
"""The drained-shield evidence (docs/verification/weapon-effects.md, "Drained shield"): the weapon_event shield_hit
rows of a flight log whose post-hit shield fraction lies below core::drained_shield_q (327 / 65535), tallied by
TBullets row, damage and owner. Reads only the weapon_event rows.

  python3 verification/results/weapon-effects/drained_rows.py [log] [--write excerpt]

--write keeps the summary, the per-owner counts and the rows below the threshold in `excerpt`
(drained_rows_run237_flight_d.txt is Run237 Flight D's, written 2026-11-06); the excerpt is itself a valid input
(its rows reproduce the tally below the threshold; the read-row total is in its header).
"""
import collections
import re
import sys

THRESHOLD = 327 / 65535


def main(path, write=None):
    read = below = 0
    by_row = collections.Counter()
    owners = collections.Counter()
    rows = []
    with open(path, errors='replace') as log:
        for line in log:
            if 'weapon_event' not in line or 'role=shield_hit' not in line or 'shield_read=1' not in line:
                continue
            f = dict(re.findall(r'(\w+)=(\S+)', line))
            read += 1
            if float(f['shield']) < THRESHOLD:
                below += 1
                by_row[(f['row'], f['damage'], f['shield'])] += 1
                owners[f['owner']] += 1
                rows.append(line.rstrip('\n'))
    out = [f'shield_hit rows with a read shield: {read}; below {THRESHOLD:.4f}: {below}; owners: {len(owners)}']
    out += [f'  row={row} damage={damage} shield={shield}: {n}' for (row, damage, shield), n in sorted(by_row.items())]
    out += [f'  owner={owner}: {n}' for owner, n in sorted(owners.items())]
    print('\n'.join(out))
    if write:
        with open(write, 'w') as f:
            f.write(f'# {path}: drained_rows.py summary, then the {below} rows below the threshold\n')
            f.write(''.join(f'# {line}\n' for line in out))
            f.write(''.join(f'{line}\n' for line in rows))


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if a != '--write']
    target = None
    if '--write' in sys.argv[1:]:
        target = sys.argv[sys.argv.index('--write') + 1]
        args.remove(target)
    main(args[0] if args else '/mnt/c/X3/x3m.log', target)
