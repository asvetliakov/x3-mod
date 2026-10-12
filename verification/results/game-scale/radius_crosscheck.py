#!/usr/bin/env python3
"""Static collision-radius estimate vs the one runtime [obj+0xa4] value on record.

docs/reverse-engineering/sector-collide.md (collide_narrow_pair F8 rows, run 44 A): radius_a = 9205962 for the
dock 'a' = TDocks index 26 at pos (-2500000, 0, 250000). The capture predates the Mayhem 4 TDocks override, so the
stock AP TDocks row 26 is evaluated (and the installed row 26 for comparison).
"""
import sys
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / 'tools/analysis')); sys.path.insert(0, str(HERE))
from sector_fog_census import Assets, STOCK_AP_CATALOGUES
import scene_radius as sr, ships

RUNTIME = 9205962
for label, a in (('stock', Assets(Path('/mnt/c/X3'), catalogues=STOCK_AP_CATALOGUES)), ('installed', Assets(Path('/mnt/c/X3')))):
    _, rows, prov = ships.table(a, 'TDocks')
    r = rows[26]
    est = sr.radii(a, r[11])
    print(f"{label} TDocks[26] ({prov['source']}) {r[5]} scene {r[11]}: estimate {est[0]} runtime {RUNTIME} "
          f"{'EQUAL' if est[0] == RUNTIME else 'differ by %+d' % (est[0] - RUNTIME)}")
