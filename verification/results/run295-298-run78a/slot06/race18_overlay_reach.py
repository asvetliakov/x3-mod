"""For every overlay body (05/06 x3m-lod.json) that is a part of a race-18 (Terran) station scene (race18_parts.txt):
records' point counts in the installed overlay body, and the final LOD the distance branch (0047d36d..0047d427 plus
the Very High -1 tail) gives in each D-R band. Merged (new_lod) reachable only if some band yields it.
usage: race18_overlay_reach.py GAME_ROOT RACE18_PARTS"""
import sys, json, collections
from pathlib import Path
here = Path(__file__).resolve(); sys.path.insert(0, str(here.parents[4] / 'tools' / 'analysis'))
import bob1
game = Path(sys.argv[1]); parts = {l.strip() for l in open(sys.argv[2]) if l.strip()}
assets = bob1._archive_modules().Assets(game)
def dist_final(k, T, P):
    n = len(T); k = min(k, n - 1)
    if k > 0:
        if T[k] < 2: k -= 1
        elif P[k] < int(P[k - 1] / 3): k -= 1   # imul 0x55555556: signed trunc division by 3
    return max(0, min(k - 1, n - 1))
res = collections.Counter()
for slot in ('05', '06'):
    for b in json.load(open(game / 'addon' / f'{slot}.x3m-lod.json'))['bodies']:
        name = b['name'].lower().replace('\\', '/')
        if name not in parts: continue
        tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, b['name'])))
        L = bob1.lods(tree); P = [len(l['points']) for l in L]; T = [100000] + [l.get('threshold', 0) for l in L[1:]]
        T = [100000] + b['thresholds'][1:]
        finals = [dist_final(k, T, P) for k in range(5)]
        reach = b['new_lod'] in finals
        res[(slot, reach)] += 1
        print(f'slot{slot} {name} points={P} thr={T[1:]} dist_final_by_band(0..4)={finals} merged_reachable={reach}')
print('summary (slot, merged reachable in distance branch) -> bodies:', dict(res))
