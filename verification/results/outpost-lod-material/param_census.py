"""Within each new opaque class (lod_atlas.effect_classes on record 0, as class_census.py), which material params
differ between members: count classes and member faces whose value != the class dominant's (strings, ints,
vectors copied from the dominant) or, for g_Mat* FLOATs (area-weighted mean), whose value differs."""
import sys
sys.path.insert(0, 'tools/analysis')
from pathlib import Path
from collections import Counter, defaultdict
import bob1, body_materials, lod_atlas, lod_overlay
game, names = Path(sys.argv[1]), [n.strip() for n in open(sys.argv[2]) if n.strip()][int(sys.argv[3]):int(sys.argv[4])]
assets, _ = body_materials.original_assets(game)
textures = lod_atlas.Textures(assets)
SKIP = {b't_diffusetexture', b't_lightmaptexture', b't_bumptexture', b't_speculartexture', b't_alphatexture'}
cls_n = Counter(); faces_n = Counter(); multi = 0; err = 0; floatdiff = defaultdict(float); bodies = defaultdict(set)
for name in names:
    try:
        e = bob1.resolve_body(assets, name)
        tree = bob1.parse(assets.read_entry(e), lod_overlay.MAX_TRAILING)
        mats, r0 = bob1.materials(tree), bob1.lods(tree)[0]
        mats = lod_atlas.classic_view(assets, mats)
        r0, _ = lod_atlas.animated_record(mats, r0, assets)
        alpha = lod_overlay.alpha_materials(mats, assets, record=r0)
        kept = lod_atlas.excluded_materials(mats, r0, alpha)
        opaque = [g for p in r0['parts'] if not p['flags'] & lod_atlas.HIDDEN_PART for g in p['groups']
                  if g['material'] not in alpha and g['material'] not in kept]
        if not opaque:
            continue
        fc = Counter()
        for g in opaque: fc[g['material']] += len(g['faces'])
        for eff, occ, mis in lod_atlas.effect_classes(mats, opaque, textures):
            if len(mis) < 2 or 'classic' in mats[mis[0]]:
                continue
            multi += 1
            d = max(mis, key=lambda m: fc[m])
            dp = {n.lower(): (t, v) for n, t, v in mats[d]['params']}
            names_ = set()
            for m in mis:
                for n, t, v in mats[m]['params']:
                    names_.add(n.lower())
            for n in names_ - SKIP:
                vals = {m: {x.lower(): (t, v) for x, t, v in mats[m]['params']}.get(n) for m in mis}
                if len({repr(v) for v in vals.values()}) > 1:
                    cls_n[n] += 1; bodies[n].add(name)
                    nf = sum(fc[m] for m in mis if repr(vals[m]) != repr(vals[d]))
                    fl = [v[1][0] / 65536 for v in vals.values() if v and v[0] == 2]
                    print('ROW', name, n.decode(), nf, f'{max(fl)/max(min(fl),1e-6):.3f}' if len(fl) == len(vals) and fl else '-', sep='\t')
                    faces_n[n] += sum(fc[m] for m in mis if repr(vals[m]) != repr(vals[d]))
                    if n.startswith(b'g_mat') and all(v and v[0] == 2 for v in vals.values()):
                        xs = [v[1][0] / 65536 for v in vals.values()]
                        floatdiff[n] = max(floatdiff[n], max(xs) / max(min(xs), 1e-6))
    except Exception as ex:
        err += 1
print('MULTI', multi, err, sep='\t')
