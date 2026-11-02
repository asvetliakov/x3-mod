#!/usr/bin/env python3
"""How many FL ship/station body stems the installed AP overlay batch already bakes (by stem name; FL ladders are
byte-identical to stock AP, lod_census_out.txt), and the batch's 'draws below T_pad' line. Counts only.
usage: overlay_coverage.py"""
import re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import catalogue_mounts  # noqa: E402
from sector_fog_census import Assets  # noqa: E402
R = Path('/mnt/c/X3FL')
fl = Assets(R, catalogues=[p.relative_to(R).as_posix() for p in catalogue_mounts.mounted_catalogues(R, 'fl')])
stems = {k[len('objects/'):-4] for k in fl.entries if k.startswith(('objects/ships/', 'objects/stations/'))
         and k.endswith(('.bob', '.bod')) and '_scene' not in k and not re.search(r'-l\d{3}\.bo[bd]$', k)}
baked = set()
for line in open('/mnt/c/X3/addon/x3m-lod-batch-bodies.txt', errors='replace'):
    m = re.match(r'(\S+): (reused|built)', line)
    if m: baked.add(m.group(1).lower())
for cls in ('ships/', 'stations/'):
    s = {x for x in stems if x.startswith(cls)}
    print(f'FL {cls[:-1]} stems={len(s)} baked by the installed AP overlay={len(s & baked)}')
for line in open('/mnt/c/X3/addon/x3m-lod-batch-summary.txt', errors='replace'):
    if line.startswith(('bodies enumerated', 'draws below T_pad', 'batch: game')):
        print(line.strip()[:260])
