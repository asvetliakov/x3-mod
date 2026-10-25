#!/usr/bin/env python3
"""Read-only census for the per-texel diffuse-strength design (docs/architecture/overlay-material-strength.md), no
bake: for each named overlay body, record 0 of its source (as class_census.py), the visible output groups of C per
part under three class keys: K2 = (effect, occlusion map) [pre 2026-10-25], K4 = K2 + cull + diffuse strength
[bef0d269, lod_atlas.effect_classes], K3 = K2 + cull only [this design: the strength moves into the atlas]. Alpha
groups count one per alpha material per part (bef0d269) under K4 and K3, one per part under K2. For every K3 class
with more than one distinct diffuse strength the face-area-weighted mean s_mean (atlas_material's rule), the class
maximum, and the ratio range s_i / s_mean are written as CLASS rows (and s_i / s_max as the minimum). One BODY row:
name, r0_drawn, draws K2, draws K4, draws K3, opaque classes K2/K4/K3, alpha groups K2/K4. CLASS rows: body,
effect, cull, two-sided-by-bump flag ('bump' when the class dominant names a bump texture), n materials,
n strengths, faces, s_mean, s_max, rmin, rmax (s_i / s_mean), min s_i / s_max.
Usage (repo root): python3 strength_census.py GAME NAMES.txt OUT.tsv [START END]"""
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


def k2(mats, m):
    mat = mats[m]
    return (lod_atlas.effect_name(mat), lod_atlas.classic_key(mat) if 'classic' in mat else
            lod_atlas.occlusion_key(mat, textures))


def k4(mats, m):
    mat = mats[m]
    return (k2(mats, m), None if 'classic' in mat else lod_atlas.material_class_key(mat))


def k3(mats, m):
    mat = mats[m]
    return (k2(mats, m), None if 'classic' in mat else lod_atlas.param_word(mat, b'g_CullMode'))


def strength(mat):
    w = lod_atlas.param_word(mat, b'g_MatDiffuseStrength')
    return None if w is None else w / 65536.0


def has_bump(mat):
    return any(t == 8 and n.lower() == b't_bumptexture' and not body_materials.is_null(v)
               for n, t, v in mat.get('params', ()))


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
            d2 = d4 = d3 = a2 = a4 = 0
            c2, c4, c3 = set(), set(), set()
            area, faces, members = {}, {}, {}
            for p in r0['parts']:
                if p['flags'] & lod_atlas.HIDDEN_PART:
                    continue
                ms = [g['material'] for g in p['groups']]
                op = [m for m in ms if m not in alpha and m not in kept]
                al = set(m for m in ms if m in alpha)
                s2, s4, s3 = {k2(mats, m) for m in op}, {k4(mats, m) for m in op}, {k3(mats, m) for m in op}
                c2 |= s2; c4 |= s4; c3 |= s3
                nk = len(set(m for m in ms if m in kept))
                d2 += len(s2) + nk + (1 if al else 0)
                d4 += len(s4) + nk + len(al)
                d3 += len(s3) + nk + len(al)
                a2 += 1 if al else 0
                a4 += len(al)
                for g in p['groups']:
                    m = g['material']
                    if m in alpha or m in kept:
                        continue
                    k = k3(mats, m)
                    members.setdefault(k, set()).add(m)
                    faces[k] = faces.get(k, 0) + len(g['faces'])
                    area[m] = area.get(m, 0.0) + sum(body_materials.face_area(r0['points'], fc) for fc in g['faces'])
            r0_drawn = sum(len(p['groups']) for p in r0['parts'] if not p['flags'] & lod_atlas.HIDDEN_PART)
            f.write(f'BODY\t{name}\t{r0_drawn}\t{d2}\t{d4}\t{d3}\t{len(c2)}\t{len(c4)}\t{len(c3)}\t{a2}\t{a4}\n')
            for k, ms in members.items():
                if 'classic' in mats[next(iter(ms))]:
                    continue
                ss = {m: strength(mats[m]) for m in ms}
                if None in ss.values() or len(set(ss.values())) < 2:
                    continue
                num = sum(area[m] * s for m, s in ss.items())
                den = sum(area[m] for m in ms)
                mean = num / den if den > 0 else 0.0
                smax = max(ss.values())
                rs = [s / mean for s in ss.values()] if mean > 0 else [0.0]
                dom = max(ms, key=lambda m: area[m])
                f.write(f'CLASS\t{name}\t{k[0][0]}\t{k[1]}\t{"bump" if has_bump(mats[dom]) else "default"}\t'
                        f'{len(ms)}\t{len(set(ss.values()))}\t{faces[k]}\t{mean:.4f}\t{smax:.4f}\t{min(rs):.4f}\t'
                        f'{max(rs):.4f}\t{min(ss.values()) / smax:.4f}\n')
        except Exception as ex:
            f.write(f'ERROR\t{name}\t{type(ex).__name__}: {str(ex)[:100]}\n')
        f.flush()
