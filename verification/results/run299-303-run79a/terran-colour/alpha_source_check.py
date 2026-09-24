"""(1) Min/mean alpha of each dock_e_tower material's diffuse (level 0) and its t_AlphaTexture name;
(2) how many faces of each alpha material share all three vertex positions with an opaque (mat0) face
(coplanar overlay decals would z-fight if merged into one opaque draw). Read-only."""
import sys
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'tools' / 'analysis'))
import bob1, body_materials, lod_atlas
name = sys.argv[1] if len(sys.argv) > 1 else 'stations/station_scenes/terran/usc_dock_e_tower'
assets = bob1._archive_modules().Assets(Path(bob1.DEFAULT_GAME))
t = bob1.parse(bob1.load(name)[0]); mats = bob1.materials(t); rec = bob1.lods(t)[0]
for i, m in enumerate(mats[:6]):
    s = body_materials.slots(m)
    d = lod_atlas.texture_bytes(assets, s.get('diffuse'))
    a = np.asarray(lod_atlas.decode_dds(d))[..., 3]
    print(f'mat{i}', s.get('diffuse').decode('latin1').split('\\')[-1], 'alpha min %d mean %.1f' % (a.min(), a.mean()),
          'alpha_tex', (s.get('alpha') or b'-').decode('latin1').split('\\')[-1])
pos = lambda i: tuple(rec['points'][i][1:4])
key = lambda f: frozenset(pos(i) for i in f[:3])
opaque = {key(f) for p in rec['parts'] for g in p['groups'] if g['material'] == 0 for f in g['faces']}
for p in rec['parts']:
    for g in p['groups']:
        if g['material']:
            n = sum(1 for f in g['faces'] if key(f) in opaque)
            print(f"mat{g['material']} faces {len(g['faces'])} coincident_with_mat0 {n}")
