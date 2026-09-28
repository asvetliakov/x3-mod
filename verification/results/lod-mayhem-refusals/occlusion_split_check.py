#!/usr/bin/env python3
"""Occlusion-split check (2026-09-28): compares a scratch batch bake from the tool before the split (HEAD
3e59cab7) with one after it, and checks the formerly occlusion_mismatch bodies in the after bake.

  python3 occlusion_split_check.py BEFORE_OUT AFTER_OUT [--game GAME]

BEFORE_OUT / AFTER_OUT are `lod_overlay.py --batch --only FILE --out DIR` roots (one addon/NN.cat each).
1. Byte identity: every body of the before bake has the same member set in the after bake and every member
   (body and atlas textures) is byte-identical (sha256 printed, first 16 hex).
2. Split bodies (in the after bake, not in the before one): source record-0 drawn groups, merged draws, merged
   materials and their occlusion maps; every merged material's raw t_OcclusionTexture string is the raw string of
   one of the source materials it absorbed, every absorbed material binds the same texture as the merged one
   (lod_atlas.occlusion_key: the entry the engine resolves; review F5, 2026-09-29), and every face of a merged group carries the second UV pairs of a record-0 face of one of
   those source materials (read from the game tree, read-only).
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / 'tools/analysis'), str(ROOT / 'verification/probe')]
import bob1                                          # noqa: E402
import lod_atlas                                     # noqa: E402
import lod_overlay                                   # noqa: E402
from inspect_x3 import read_catalogue                # noqa: E402
from sector_fog_census import unpack                 # noqa: E402


def overlay(root):
    cat = next((Path(root) / 'addon').glob('*.cat'))
    raw = bytes(v ^ 0x33 for v in cat.with_suffix('.dat').read_bytes())
    members = {e['path']: raw[e['offset']:e['offset'] + e['size']] for e in read_catalogue(cat)}
    marker = json.loads(cat.with_name(cat.stem + '.x3m-lod.json').read_text())
    record = json.loads((Path(root) / 'x3m-lod-batch.json').read_text())
    return members, {b['name']: b for b in marker['bodies']}, {b['name']: b for b in record['bodies']}, record


def body_members(b):
    return sorted(m if isinstance(m, str) else m.get('path', m.get('member')) for m in b['members'])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('before')
    ap.add_argument('after')
    ap.add_argument('--game', type=Path, default=bob1.DEFAULT_GAME)
    a = ap.parse_args()
    bm, bmark, _, brec = overlay(a.before)
    am, amark, arec, rec = overlay(a.after)
    print(f'tool_sha256 before {brec["settings"]["tool_sha256"][:16]} after {rec["settings"]["tool_sha256"][:16]}')
    print('== byte identity (bodies baked before the split)')
    same_all = True
    for name in sorted(bmark):
        names = body_members(bmark[name])
        ok = names == body_members(amark.get(name, {'members': []}))
        for p in names:
            ok = ok and bm[p] == am.get(p)
        same_all &= ok
        body = next(p for p in names if p.startswith('objects/'))
        print(f'{name}: members {len(names)} identical={ok} body_sha256 {hashlib.sha256(bm[body]).hexdigest()[:16]}'
              f' after {hashlib.sha256(am[body]).hexdigest()[:16]}')
    print(f'all identical: {same_all}')
    print(f'refused {rec["refused"]} filtered {rec["filtered"]}')
    print('== split bodies (in the after bake only)')
    assets, _ = lod_overlay.original_assets(a.game)
    textures = lod_atlas.Textures(assets)
    for name in sorted(set(amark) - set(bmark), key=str.lower):
        m, r = amark[name], arec[name]
        body = next(p for p in body_members(m) if p.startswith('objects/'))
        tree = bob1.parse(unpack(am[body]), lod_overlay.MAX_TRAILING)
        mats, new = bob1.materials(tree), bob1.lods(tree)[m['new_lod']]
        src = bob1.parse(assets.read_entry(bob1.resolve_body(assets, name)), lod_overlay.MAX_TRAILING)
        smats, r0 = bob1.materials(src), bob1.lods(src)[0]
        uv2 = lambda pts, f: tuple(pts[i][6:8] if pts[i][0] & 4 else None for i in f[:3])
        faces = {}
        for p in r0['parts']:
            for g in p['groups']:
                faces.setdefault(g['material'], set()).update(uv2(r0['points'], f) for f in g['faces'])
        rows = [s for s in m['synth'] if s.get('atlas') and not s.get('widened')]
        key = lambda mat: lod_atlas.occlusion_key(mat, textures)
        raw_ok = all(lod_atlas.occlusion_raw(mats[s['index']]) in [lod_atlas.occlusion_raw(smats[x]) for x in s['absorbed']]
                     for s in rows)
        occl_ok = raw_ok and all(key(mats[s['index']]) == key(smats[x]) for s in rows for x in s['absorbed'])
        keys = [key(mats[s['index']]) for s in rows]
        occl_ok = occl_ok and len({(s['effect'], k) for s, k in zip(rows, keys)}) == len(rows)   # one class per texture
        uv_ok, checked = True, 0
        for p in new['parts']:
            if p['flags'] & lod_atlas.HIDDEN_PART:
                continue
            for g in p['groups']:
                row = next((s for s in rows if s['index'] == g['material']), None)
                if row is None:
                    continue
                allowed = set().union(*(faces.get(x, set()) for x in row['absorbed']))
                for f in g['faces']:
                    checked += 1
                    uv_ok &= uv2(new['points'], f) in allowed
        maps = [lod_atlas.occlusion_label(mats[s['index']]) for s in rows]
        print(f'{name}: outcome {"baked" if r["eligible"] else "filtered " + ",".join(r["filter"])}'
              f' r0_drawn {r["r0_drawn"]} merged_draws {r["draws"]} merged_materials {len(rows)}'
              f' maps {maps} bound {keys} raw_string_of_a_source={raw_ok} same_bound_texture_as_sources={occl_ok}'
              f' uv2_faces_checked {checked} uv2_kept={uv_ok}')


if __name__ == '__main__':
    main()
