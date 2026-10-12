#!/usr/bin/env python3
"""Laser ranges from TLaser + TBullets (installed and stock).

Engine getters (owning note section 6): 0x00433e90 range(laser) = TBullets[TLaser[l]+0x4c].(+0x50 * +0x54) / 1000,
0x00433ee0 speed(laser) = TBullets[...]+0x54. TBullets col 10 -> +0x50 (lifetime, ms), col 11 -> +0x54 (speed, engine
units/s; loader 0x00436518). TLaser col 9 -> +0x4c (bullet index). 500 engine units = 1 m.
Writes weapons.json beside this script and prints a summary.
"""
import json, statistics, sys
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / 'tools/analysis')); sys.path.insert(0, str(HERE))
from sector_fog_census import Assets, names, STOCK_AP_CATALOGUES
import ships


def lasers(a):
    res, _ = names(a)
    _, lrows, lp = ships.table(a, 'TLaser'); _, brows, bp = ships.table(a, 'TBullets')
    out = []
    for r in lrows:
        if len(r) < 10 or not r[9].lstrip('-').isdigit(): continue
        bi = int(r[9])
        if not 0 <= bi < len(brows): continue
        b = brows[bi]; life, spd = int(b[10]), int(b[11])
        out.append(dict(id=r[-2] if r[-1] == '' else r[-1], subtype=r[5], name=res(17, int(r[6])) if r[6].isdigit() else '',
                        bullet=bi, lifetime_ms=life, speed_mps=spd / 500, range_m=spd * life / 1000 / 500))
    return out, lp, bp


def main():
    res = {}
    for label, a in (('installed', Assets(Path('/mnt/c/X3'))), ('stock', Assets(Path('/mnt/c/X3'), catalogues=STOCK_AP_CATALOGUES))):
        ls, lp, bp = lasers(a)
        rng = sorted(l['range_m'] for l in ls if l['range_m'] > 0)
        res[label] = dict(tlaser=lp, tbullets=bp, n=len(ls), range_m=dict(min=rng[0], p25=rng[len(rng) // 4], median=statistics.median(rng),
                          p75=rng[3 * len(rng) // 4], max=rng[-1]),
                          speed_mps_median=statistics.median(l['speed_mps'] for l in ls), lasers=sorted(ls, key=lambda l: l['range_m']))
        print(label, lp['source'], bp['source'], 'n', len(ls), 'range m', res[label]['range_m'], 'speed med', res[label]['speed_mps_median'])
        for l in res[label]['lasers'][::max(1, len(ls) // 8)]:
            print(f"   {l['name'][:34]:34s} {l['range_m']:8.0f} m  {l['speed_mps']:6.0f} m/s  {l['lifetime_ms']} ms")
    (HERE / 'weapons.json').write_text(json.dumps(res, indent=1))


if __name__ == '__main__':
    main()
