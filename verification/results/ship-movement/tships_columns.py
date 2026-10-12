#!/usr/bin/env python3
"""Column -> record-offset cross-check for docs/reverse-engineering/ship-movement.md section 7.

  python3 verification/results/ship-movement/tships_columns.py [TShips.txt]

Reads a TShips.txt (default: the copy under /mnt/c/X3/ZMap; whether it equals the
table packed in the game's cat/dat is not established here) and prints, for the
columns the movement code reads, the record offset the loader 0x00434e40 stores
them at, three sample rows and the value distribution of the two columns that set
per-ship timing (reaction delay, rotation acceleration).
"""
import collections
import sys

path = sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3/ZMap/TShips.txt'
lines = [l.rstrip('\n').split(';') for l in open(path, encoding='latin1') if not l.startswith('//')]
version, count = int(lines[0][0]), int(lines[0][1])
rows = [r for r in lines[1:] if len(r) > 53]
print(f'{path}: version {version}, header count {count}, rows {len(rows)}')
COLS = [(2, '+0x08', 'rotation X (yaw rate), decimal x 65536'),
        (3, '+0x0c', 'rotation Y (pitch rate), decimal x 65536'),
        (4, '+0x10', 'rotation Z (roll rate), decimal x 65536'),
        (5, '+0x14', 'ship class (SG_SH_*)'),
        (7, '+0x44', 'max speed'),
        (8, '+0x48', 'acceleration'),
        (10, '+0x50', 'reaction delay (x 50 ms think interval)'),
        (51, '+0xd0', 'rotation acceleration (% of max rate per s)')]
for c, o, what in COLS:
    print(f'col {c:2d} -> record {o:6s} {what:46s} ' + ' | '.join(rows[i][c] for i in (0, 1, 19)))
for c in (10, 51):
    print(f'col {c} distribution', collections.Counter(r[c] for r in rows).most_common(12))
