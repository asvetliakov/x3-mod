"""Materials and LOD groups of one body: every mounted member (source and overlay) on an FL install.
Usage: X3M_EDITION=fl body_materials.py GAME BODYNAME"""
import sys, collections
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'tools/analysis'))
import bob1
game, name = Path(sys.argv[1]), sys.argv[2]
sfc = bob1._archive_modules()
assets = sfc.Assets(game)
won = bob1.resolve_body(assets, name)
for e in bob1.body_members(assets, name):
    tree = bob1.parse(assets.read_entry(e), max_trailing=64)
    print('== member %s:%s winner=%s' % (e['source'], e['path'], e is won))
    mats = bob1.materials(tree)
    for m in mats:
        dec = lambda x: x.decode('latin1') if isinstance(x, bytes) else x
        tex = {dec(p[0]): dec(p[2]) for p in m.get('params', []) if p[1] == 8}
        num = {dec(p[0]): p[2] for p in m.get('params', []) if p[1] != 8}
        print(' mat %d fx=%s tech=%s' % (m['index'], m.get('effect'), m.get('technique')), {k: v.split('\\')[-1] for k, v in tex.items()},
              {k: v for k, v in num.items() if k in ('g_MatDiffuseStrength', 'g_MatEmissive', 'g_MatAlpha')})
    for li, lod in enumerate(bob1.lods(tree)):
        faces = collections.Counter()
        for p in lod['parts']:
            for g in p['groups']:
                faces[g['material']] += len(g['faces'])
        print(' lod %d value=%d points=%d faces_by_material=%s' % (li, lod['value'], len(lod['points']), dict(sorted(faces.items()))))
