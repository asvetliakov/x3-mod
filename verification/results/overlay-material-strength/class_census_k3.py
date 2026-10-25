#!/usr/bin/env python3
"""Read-only draw census of the option-B class key (docs/architecture/overlay-material-strength.md 8), no bake: for
each named overlay body, record 0 of its source (overlay catalogues skipped, as the baker reads it; classic_view,
animated_record, alpha_materials and excluded_materials as lod_batch_census.census_body), the visible output groups
of C per part under K2 (the installed overlay's key: effect + occlusion map; one alpha group per part) and under the
current baker (lod_atlas.effect_classes: effect + occlusion map + lod_atlas.material_class_key, today g_CullMode
alone; one alpha group per part, the restored alpha path), and record 0's own drawn groups. Kept effects and hidden
parts count the same both ways; widened groups, light_bleed keeps, recipes and the 60,000-point group split are not
modelled. One TSV row per body: name, r0_drawn, before, after, opaque classes before/after, alpha groups (same both
ways), bodies whose class members differ in g_MatDiffuseStrength (the classes the ratio alpha serves).
Usage (repo root): python3 class_census_k3.py GAME NAMES.txt OUT.tsv [START END]"""
import sys
sys.path.insert(0, 'tools/analysis')
from pathlib import Path
import bob1, body_materials, lod_atlas, lod_overlay

game, names_file, out = Path(sys.argv[1]), sys.argv[2], sys.argv[3]
names = [n.strip() for n in open(names_file) if n.strip()]
if len(sys.argv) > 5:
    names = names[int(sys.argv[4]):int(sys.argv[5])]
assets, _ = body_materials.original_assets(game)
textures = lod_atlas.Textures(assets)


def old_key(mats, m):
    mat = mats[m]
    return (lod_atlas.effect_name(mat), lod_atlas.classic_key(mat) if 'classic' in mat else
            lod_atlas.occlusion_key(mat, textures))


def new_key(mats, m):
    mat = mats[m]
    return (old_key(mats, m), None if 'classic' in mat else lod_atlas.material_class_key(mat))


with open(out, 'a') as f:
    for name in names:
        try:
            e = bob1.resolve_body(assets, name)
            tree = bob1.parse(assets.read_entry(e), lod_overlay.MAX_TRAILING)
            mats, r0 = bob1.materials(tree), bob1.lods(tree)[0]
            mats = lod_atlas.classic_view(assets, mats)
            r0, _ = lod_atlas.animated_record(mats, r0, assets)
            alpha = lod_overlay.alpha_materials(mats, assets, record=r0)
            kept = lod_atlas.excluded_materials(mats, r0, alpha)
            before = after = ab = 0
            all_b, all_a, members = set(), set(), {}
            for p in r0['parts']:
                if p['flags'] & lod_atlas.HIDDEN_PART:
                    continue
                ms = [g['material'] for g in p['groups']]
                op = [m for m in ms if m not in alpha and m not in kept]
                al = set(m for m in ms if m in alpha)
                kb = {old_key(mats, m) for m in op}
                ka = {new_key(mats, m) for m in op}
                for m in op:
                    members.setdefault(new_key(mats, m), set()).add(lod_atlas.param_word(mats[m], lod_atlas.STRENGTH_PARAM))
                all_b |= kb
                all_a |= ka
                nk = len(set(m for m in ms if m in kept))
                before += len(kb) + nk + (1 if al else 0)
                after += len(ka) + nk + (1 if al else 0)
                ab += 1 if al else 0
            multi = sum(1 for k, v in members.items() if 'classic' not in mats[0] and len(v) > 1)
            r0_drawn = sum(len(p['groups']) for p in r0['parts'] if not p['flags'] & lod_atlas.HIDDEN_PART)
            f.write(f'{name}\t{r0_drawn}\t{before}\t{after}\t{len(all_b)}\t{len(all_a)}\t{ab}\t{multi}\n')
        except Exception as ex:
            f.write(f'{name}\terror\t{type(ex).__name__}: {str(ex)[:100]}\n')
        f.flush()
