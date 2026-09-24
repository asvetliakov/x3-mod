# Per-record material face counts and y ranges of the panes (mat 21) and beams (mat 4): the vanilla LOD ladder of
# terran_spp_panel (record 2 flattens the panes to one plane and keeps the beams). Reads the bottle's catalogues only.
import sys; sys.path.insert(0, 'tools/analysis')
from pathlib import Path
from collections import Counter
import numpy as np, bob1, lod_overlay
game = Path.home()/'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
assets, _ = lod_overlay.original_assets(game)
tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, 'stations/x3tc/terran_spp_panel')), lod_overlay.MAX_TRAILING)
for k, l in enumerate(bob1.lods(tree)):
    c = Counter()
    for p in l['parts']:
        for g in p['groups']: c[g['material']] += len(g['faces'])
    P = np.array([[p[1], p[2], p[3]] for p in l['points']], float)
    def yr(m):
        idx = sorted({i for p in l['parts'] for g in p['groups'] if g['material'] == m for f in g['faces'] for i in f[:3]})
        return (float(P[idx][:, 1].min()), float(P[idx][:, 1].max())) if idx else None
    print(f'record {k} T={l["value"]} faces {sum(c.values())}: mat21 {c.get(21,0)} y{yr(21)} mat4 {c.get(4,0)} y{yr(4)} mat9 {c.get(9,0)} mat6 {c.get(6,0)} mat18 {c.get(18,0)}; parts {len(l["parts"])}')
