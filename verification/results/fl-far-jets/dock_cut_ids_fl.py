#!/usr/bin/env python3
"""FL dock-port cut-scene ids (read-only; prints counts, rows and hashes only, no game bytes).

The small-parts stub's dock rule (cull_small_parts_core.h dock_model) treats model ids 901300000..901499999 and
909800000..909999999 as dock-port parts: the inline bodies of cut scenes 9013/9014 (and 9098/9099) at
`local + (cut - 1) * 100000` (FL arithmetic at 0x4ae169 / 0x4ad62d, verification/results/fl-cull-chain/contract.json).
On AP the ids were established from data (verification/results/ship-scene-parts/dock_port_data.py). This script checks
the same chain in the FL mount (root NN.cat + addon2/NN.cat, catalogue_mounts 'fl'): the types/Dummies rows of the
dock-port dummies, the types/CutData rows of their cut ids, the cut scenes' inline bodies (`P n; B 1000nn;` with an
`L {` block) and therefore the model ids they produce.

usage: python3 -I verification/results/fl-far-jets/dock_cut_ids_fl.py [game root, default /mnt/c/X3FL]
Each cut reports `coverage`: in_ranges (inline-body ids, all inside the stub's ranges), not_covered_named_bodies (no
inline body; the scene names body files, which take body-table ids outside the ranges), outside_ranges or unresolved.
Exit 0 when the four dummies name cuts 9013/9014/9098/9099, every scene and named body file resolves, and no inline
id falls outside the ranges; `cuts_not_covered` lists the cuts the dock rule does not reach.
"""
import json
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / 'tools/analysis'))
from sector_fog_census import Assets  # noqa: E402

DUMMIES = ('19026', '19027', '19098', '19099')
CUTS = ('9010', '9013', '9014', '9098', '9099')
PART = re.compile(rb'^P (\d+); B ([^;]+);', re.M)
FIRST, SECOND, SPAN = 901300000, 909800000, 200000


def dock_model(i):
    return 0 <= i - FIRST < SPAN or 0 <= i - SECOND < SPAN


def get(assets, path):
    try:
        return assets.get(path)
    except FileNotFoundError:
        return None, None


def rows(data, keys):
    out = {}
    for line in data.decode('latin1').splitlines():
        key = line.split(';')[0].strip()
        if key in keys:
            out[key] = line.strip()
    return out


def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3FL')
    assets = Assets(root, edition='fl')
    result = dict(game=str(root), edition=assets.edition, layers=len(assets.layers))
    dummies, prov = get(assets, 'types/Dummies.txt')
    result['dummies'] = dict(source=prov['source'], member=prov['member'], sha256=prov['decoded_sha256'][:16],
                             rows=rows(dummies, DUMMIES))
    cuts, prov = get(assets, 'types/CutData.txt')
    cut_rows = rows(cuts, CUTS)
    result['cutdata'] = dict(source=prov['source'], member=prov['member'], sha256=prov['decoded_sha256'][:16],
                             rows=cut_rows)
    scenes, ok = {}, True
    for cut in ('9013', '9014', '9098', '9099'):
        row = cut_rows.get(cut)
        if not row:
            ok = False
            scenes[cut] = 'no CutData row'
            continue
        scene = row.split(';')[1].strip().replace('\\', '/')
        data, prov = None, None
        for ext in ('.bod', '.pbd'):
            data, prov = get(assets, f'objects/{scene}{ext}')
            if data is not None:
                break
        if data is None:
            ok = False
            scenes[cut] = f'{scene}: absent'
            continue
        parts = [(int(m.group(1)), m.group(2).decode('latin1').strip()) for m in PART.finditer(data)]
        inline = [int(b) for _, b in parts if b.isdigit() and int(b) >= 100000]
        ids = [local + (int(cut) - 1) * 100000 for local in inline]
        # An empty list is not coverage: a scene without inline bodies names body files, whose model ids are
        # body-table ids (id -> slot map 0x4867c0), never local + (cut - 1) * 100000.
        named_files = {}
        for _, body in parts:
            if body.isdigit():
                continue
            stem = 'objects/' + body.replace('\\', '/')
            found = [get(assets, stem + ext)[1] for ext in ('.pbb', '.bob', '.pbd', '.bod')]
            named_files[body] = next((f['source'] for f in found if f), None)
        if not ids:
            coverage = 'not_covered_named_bodies' if named_files else 'unresolved'
        else:
            coverage = 'in_ranges' if all(dock_model(i) for i in ids) else 'outside_ranges'
        ok &= coverage not in ('outside_ranges', 'unresolved') and all(named_files.values())
        scenes[cut] = dict(scene=scene, member=prov['member'], source=prov['source'],
                           sha256=prov['decoded_sha256'][:16], parts=len(parts),
                           inline_blocks=len(re.findall(rb'^L \{', data, re.M)), inline_bodies=inline,
                           model_ids=ids, named_body_files=named_files, coverage=coverage)
    result['scenes'] = scenes
    result['cuts_in_dock_ranges'] = [c for c, v in scenes.items() if isinstance(v, dict) and v['coverage'] == 'in_ranges']
    result['cuts_not_covered'] = [c for c in scenes if c not in result['cuts_in_dock_ranges']]
    # The dock-port dummies name exactly these cuts (column 5 of types/Dummies), as on AP.
    named = {k: v.split(';')[4].strip() for k, v in result['dummies']['rows'].items()}
    result['dummy_cuts'] = named
    ok &= named == {'19026': '9013', '19027': '9014', '19098': '9098', '19099': '9099'}
    result['passed'] = ok
    print(json.dumps(result))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
