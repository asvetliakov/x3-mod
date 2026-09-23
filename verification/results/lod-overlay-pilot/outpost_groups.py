import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import bob1, lod_overlay, lod_atlas
assets, _ = lod_overlay.original_assets(bob1.DEFAULT_GAME)
tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, 'Stations/others/military_outpost_middleb')))
mats = bob1.materials(tree)
alpha = lod_overlay.alpha_materials(mats)
for r in (0, 1):
    rec = bob1.lods(tree)[r]
    src_groups = [len({i for f in g['faces'] for i in f[:3]}) for p in rec['parts'] for g in p['groups']]
    res = lod_atlas.collapse(assets, 'military_outpost_middleb', list(mats), rec, alpha, 150, (1024, 2048), True)
    c = res['record']
    per = [(g['material'], len(g['faces']), len({i for f in g['faces'] for i in f[:3]})) for p in c['parts'] for g in p['groups']]
    print(f'record {r}: points {len(rec["points"])}, max distinct points per source group {max(src_groups)};'
          f' C points {len(c["points"])} (dup {res["info"]["duplicated"]}); C groups (material, faces, distinct points) {per};'
          f' atlas {res["layout"]["size"]} min texels/px {res["layout"]["min_ratio"]:.3f}')
