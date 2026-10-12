#!/usr/bin/env python3
"""Proposed steering/avoidance parameters (for the design agent) evaluated on representative installed ships.

Inputs per ship (installed TShips, base values, ships.py units): v (m/s), a (m/s^2), w = slower of rot X/Y (rad/s),
r = static collision radius estimate (m). Proposals (owning note section 9):
  R      = v / w                                   turning radius at speed v
  C      = r_own + r_obs + m,  m = max(25 m, 0.5 r_own)       clearance (sphere-sphere)
  D_turn = sqrt(C (C + 2 R))                        distance to obstacle centre at which a full-rate turn still clears it
  D_stop = v^2 / (2 a)                              braking distance (table acceleration used as deceleration: inferred)
  L      = clamp(v t_r + k min(D_turn, D_stop), C + r_own, 30 km),  t_r = 0.5 s, k = 1.25
  arrive = max(2 r_own, R / 4)                      (R at v_max: R/4 is the radius at a quarter-speed approach)
  space  = r_lead + r_foll + max(50 m, 0.5 (r_lead + r_foll))  vs engine KC 4 x GetSize (base ship class)
Writes recommend.json beside this script and prints the table.
"""
import json, math, sys
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / 'tools/analysis')); sys.path.insert(0, str(HERE))
from sector_fog_census import Assets
import ships

REPS = [('M5', 'Discoverer'), ('M3', 'Nova'), ('M6', 'Centaur'), ('TS', 'Mercury'), ('M2', 'Boreas')]
T_R, K, LMAX = 0.5, 1.25, 30000.0
OBSTACLES = dict(same_class=None, station_1500m=1500.0, asteroid_60m=60.0)


def params(s, r_obs):
    v, a = s['speed'], s['accel']; w = math.radians(min(s['rot_dps'][:2])); r = s['radius_u'] / 500
    R = v / w; m = max(25.0, 0.5 * r)
    r_obs = r if r_obs is None else r_obs
    C = r + r_obs + m
    d_turn = math.sqrt(C * (C + 2 * R)); d_stop = v * v / (2 * a)
    L = min(max(v * T_R + K * min(d_turn, d_stop), C + r), LMAX)
    return dict(C=C, d_turn=d_turn, d_stop=d_stop, L=L, T=L / v)


def main():
    a = Assets(Path('/mnt/c/X3'))
    si, _ = ships.ships(a)
    out = []
    for cls, name in REPS:
        c = [s for s in si if s['name'] == name and s['subtype'] == 'SG_SH_' + cls and s['variation'] in (0, None)] or \
            [s for s in si if s['name'] == name and s['subtype'] == 'SG_SH_' + cls]
        s = c[0]
        v, w = s['speed'], math.radians(min(s['rot_dps'][:2])); r = s['radius_u'] / 500; R = v / w
        row = dict(cls=cls, name=name, v=v, a=s['accel'], w_dps=round(math.degrees(w), 2), r=round(r, 1), R=round(R),
                   t180=round(math.pi / w, 2), d_stop=round(v * v / (2 * s['accel'])),
                   arrive=round(max(2 * r, R / 4)), space_same=round(2 * r + max(50, r)), engine_formation_4x=round(4 * r))
        for k, ro in OBSTACLES.items():
            p = params(s, ro)
            row['L_' + k] = round(p['L']); row['T_' + k] = round(p['T'], 1); row['C_' + k] = round(p['C'])
            row['dturn_' + k] = round(p['d_turn'])
        out.append(row)
    (HERE / 'recommend.json').write_text(json.dumps(out, indent=1))
    keys = list(out[0])
    print(' | '.join(keys))
    for row in out: print(' | '.join(str(row[k]) for k in keys))


if __name__ == '__main__':
    main()
