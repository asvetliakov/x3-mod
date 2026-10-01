"""Particles3 rows per view: id, comment, emitter materials (which ids share a material batch with ship trails).
Read-only; ~5 s. python3 verification/results/engine-effects-census/particles3_materials.py"""
import sys, re, collections
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from sector_fog_census import Assets, STOCK_AP_CATALOGUES  # noqa: E402
import lod_overlay  # noqa: E402
import engine_effects_census as c  # noqa: E402
GAME = c.GAME
views = {'stock': Assets(GAME, catalogues=STOCK_AP_CATALOGUES), 'mayhem': lod_overlay.original_assets(GAME)[0]}
for name, a in views.items():
    par = c.particles(a)
    ships_t, _ = c.text(a, 'types/TShips.txt')
    used = collections.Counter()
    for line in ships_t.splitlines():
        toks = line.split(';')
        if len(toks) > 49 and toks[0].strip().isdigit() and toks[49].strip().lstrip('-').isdigit():
            used[int(toks[49])] += 1
    # TMissiles: column 'trail' candidates -> list Particles3 ids referenced anywhere in TMissiles/TBullets tokens is
    # ambiguous; report by Particles3 comment instead.
    print(f'== {name}: {len(par)} ids')
    bymat = collections.defaultdict(list)
    for pid, row in sorted(par.items()):
        mats = sorted({e['mat'] for e in row['emitters']})
        flags = sorted({e['flags'] for e in row['emitters']})
        tag = 'SHIP' if used.get(pid) else '    '
        print(f'  {pid:3d} {tag} ships={used.get(pid,0):3d} mats={mats} flags={flags} :: {row["comment"][:70]}')
        for m in mats: bymat[m].append((pid, tag.strip() or 'other'))
    print('  material -> ids:')
    for m, ids in sorted(bymat.items()):
        print(f'    {m}: ship={[p for p,t in ids if t=="SHIP"]} other={[p for p,t in ids if t!="SHIP"]}')
