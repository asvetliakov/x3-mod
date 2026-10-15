#!/usr/bin/env python3
"""Lane fix round 2 review item 10: the installed ships (Mayhem 4 TShips, game-scale/ships.py's reader) whose radius or hull half-extent
exceeds kLaneHullMaxM (1,300 m, ship_gate_ctl_core.h), with their speed and acceleration, against the Terran ring's d_end (4,147 m:
front 1,597 + kLaneHullMaxM + kLaneRunMaxM 1,100 + 150). Reads the game's CAT/DAT under /mnt/c/X3 read-only; prints derived numbers only.
Usage: python3 long_hulls.py > long_hulls.txt"""
import sys
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / 'game-scale'))
sys.path.insert(0, str(HERE.parents[4] / 'tools/analysis'))
import ships as sh

U = 500.0
HULL_MAX, RUN_MAX, FRONT = 1300.0, 1100.0, 0.498 * 3207.5
inst = sh.Assets(sh.X3)
rows, _ = sh.ships(inst)
cap = [s for s in rows if s['cls'] in ('OBJ_SHIP_M1', 'OBJ_SHIP_M2', 'OBJ_SHIP_TL', 'OBJ_SHIP_M7')]
print('# installed capitals (M1/M2/TL/M7): %d; radius or hull half-extent > %.0f m:' % (len(cap), HULL_MAX))
print('# class | name or subtype | radius m | hull half-extent m | speed m/s | accel m/s^2 | stop at 100 m/s m | d_end needed m')
n = 0
for s in sorted(cap, key=lambda s: -(s['radius_u'] or 0)):
    r = (s['radius_u'] or 0) / U
    h = (s['hull_u'] or 0) / U
    if max(r, h) <= HULL_MAX:
        continue
    n += 1
    v = min(100.0, s['speed'])
    stop = v * v / (2 * s['accel']) + v if s['accel'] > 0 else float('inf')
    print('%s | %s | %.0f | %.0f | %.1f | %.2f | %.0f | %.0f' % (s['cls'][9:], s['name'] or s['subtype'], r, h, s['speed'], s['accel'], stop,
                                                                FRONT + max(r, h) + stop + 150.0))
print('# %d of %d over %.0f m' % (n, len(cap), HULL_MAX))
