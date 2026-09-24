"""For each installed overlay body: the coarse record's alpha group (lod_atlas.rewrite_record class 'alpha',
drawn with the dominant alpha material's textures) and, per absorbed source material, the faces and the area
share of record 0 whose diffuse differs from the dominant's (they are drawn with the wrong texture on C).
Usage: python3 alpha_group_loss.py [NAME_SUBSTRING ...]  (default: every addon/06 body)"""
import sys, json
from collections import defaultdict
from pathlib import Path
ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
import bob1, body_materials, lod_overlay
ADDON = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/addon'
subs = [s.lower() for s in sys.argv[1:]]
recs = [b for s in ('06',) for b in json.load(open(ADDON / f'{s}.x3m-lod.json'))['bodies']]
if subs:
    recs = [b for b in recs if any(s in b['name'].lower() for s in subs)]
def short(t):
    return t.decode('latin1').replace('/', '\\').split('\\')[-1] if t else '-'
tot = defaultdict(float)
for b in recs:
    data, _ = bob1.load(b['name'])
    tree = bob1.parse(data)
    mats = bob1.materials(tree)[:b['source_materials']]
    rec = bob1.lods(tree)[b.get('source_record', 0)]
    alpha = lod_overlay.alpha_materials(mats)
    faces, area = defaultdict(int), defaultdict(float)
    total_area = 0.0
    for p in rec['parts']:
        for g in p['groups']:
            a = sum(body_materials.face_area(rec['points'], f) for f in g['faces'])
            total_area += a
            if g['material'] in alpha:
                faces[g['material']] += len(g['faces']); area[g['material']] += a
    if not faces:
        print(b['name'], 'no alpha group'); continue
    dom = max(faces, key=lambda m: faces[m])
    dtex = body_materials.slots(mats[dom]).get('diffuse')
    parts = []
    wrong = 0.0
    for m in sorted(faces):
        tex = body_materials.slots(mats[m]).get('diffuse')
        bad = tex != dtex
        if bad:
            wrong += area[m]
        parts.append(f'm{m}:{short(tex)}:{faces[m]}f:{100 * area[m] / total_area:.1f}%{"*" if bad else ""}')
    print(b['name'].split('/')[-1], f'dominant=m{dom}:{short(dtex)}', 'wrong_tex_area=%.1f%%' % (100 * wrong / total_area), ' '.join(parts))
