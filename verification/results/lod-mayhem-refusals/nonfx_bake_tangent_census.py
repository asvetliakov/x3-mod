#!/usr/bin/env python3
"""Classic (non-effect) material bake, 2026-09-29: where the engine lights a classic record with the tilted
normal of the NONE_NORMAL placeholder, and which bodies the baker's tangent-declaration guard touches.

  python3 nonfx_bake_tangent_census.py [--game GAME] [--jobs N] > nonfx_bake_tangent_census_out.txt

Read-only over the installed batch record (<game>/addon/x3m-lod-batch.json) and the winning bodies.
Facts used (docs/reverse-engineering/non-effect-materials.md section 6): a classic record with a bump or light
map draws BUMPMAP_LOW; without a bump map of its own the NONE_NORMAL placeholder is bound (device byte +0xa0 = 1
on the 2_0 .. 3_0 profiles) and read as 2 rgb - 1; the model has tangents only with the tangent declaration
(lod_atlas.tangent_declared) and a vertex only where its group carries tangent records in a part with flags
0x30000000.
A. The ship/station bodies the installed record refuses non_effect_material: record-0 faces of reproducible
   classic records by (technique, tangent declaration, tangent records).
B. Every ship/station body of the record with a classic record in its material table: bodies without the
   tangent declaration whose table holds a BUMPMAP_LOW classic record and an effect material declaring
   t_BumpTexture (the case in which a bump atlas would introduce the declaration; lod_atlas.collapse bakes no
   bump atlas there), split by whether the installed record bakes the body today.
"""
import argparse
import collections
import json
import multiprocessing
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / 'tools/analysis'), str(ROOT / 'verification/probe')]
import bob1                                          # noqa: E402
import lod_atlas                                     # noqa: E402
import lod_overlay                                   # noqa: E402

_W = {}


def init(game):
    _W['assets'] = lod_overlay.original_assets(Path(game))[0]
    _W['by'] = {f'{e["source"]}:{e["path"]}'.lower(): e for lst in _W['assets'].entries.values() for e in lst}


def scan(row):
    assets = _W['assets']
    entry = _W['by'].get(row['member'].lower())
    if entry is None:
        return None
    try:                       # a text body compiles as the engine's text loader reads it: no tangent records
        data = assets.read_entry(entry)
        tree = (bob1.parse(data, lod_overlay.MAX_TRAILING) if entry['path'].lower().endswith(('.pbb', '.bob'))
                else bob1.parse_text(data))
    except bob1.FormatError:
        return None
    finally:
        assets.cache.clear()
    mats = bob1.materials(tree)
    if all('params' in m for m in mats):
        return None
    view = lod_atlas.classic_view(assets, mats)
    declared = lod_atlas.tangent_declared(mats)
    shape = lambda m: m.get('classic') or m.get('classic_shape')
    faces = collections.Counter()
    for part in bob1.lods(tree)[0]['parts']:
        if part['flags'] & lod_atlas.HIDDEN_PART:
            continue
        for g in part['groups']:
            if 0 <= g['material'] < len(view) and 'classic' in view[g['material']]:
                records = bool(part['flags'] & 0x30000000) and bool(g.get('extra'))
                faces['BUMPMAP_LOW' if shape(view[g['material']])['low'] else 'DEFAULT',
                      'declared' if declared else 'not declared', 'records' if records else 'no records'] += len(g['faces'])
    low = any(shape(m) and shape(m)['low'] for m in view)
    bump_param = any(lod_atlas.is_effect(m) and lod_atlas.SLOT_NAMES['bump'] in lod_atlas.declared(m) for m in view)
    by_effect = lod_atlas.tangent_declared([m for m in mats if 'params' in m])
    return dict(name=row['name'], eligible=bool(row.get('eligible')), refuse=row.get('refuse') or [],
                text=not entry['path'].lower().endswith(('.pbb', '.bob')),
                declared_by='effect bump map' if by_effect else 'classic bump name' if declared else None,
                declared=declared, faces=dict((' / '.join(k), v) for k, v in faces.items()),
                guard=not declared and low and bump_param)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', type=Path, default=bob1.DEFAULT_GAME)
    ap.add_argument('--jobs', type=int, default=6)
    a = ap.parse_args()
    rec = json.loads((a.game / 'addon' / 'x3m-lod-batch.json').read_text())
    rows = [b for b in rec['bodies'] if b['cat'] != 'other']
    with multiprocessing.Pool(a.jobs, init, (str(a.game),)) as pool:
        out = [r for r in pool.map(scan, rows, chunksize=16) if r]
    print(f'ship/station bodies of the record: {len(rows)}; with a classic record in the material table: {len(out)}')
    former = [r for r in out if 'non_effect_material' in r['refuse']]
    print(f'A. formerly non_effect_material bodies read: {len(former)}')
    faces, bodies = collections.Counter(), collections.Counter()
    for r in former:
        for k, v in r['faces'].items():
            faces[k] += v
            bodies[k] += 1
    for k in sorted(faces):
        print(f'   {k}: faces {faces[k]}, bodies {bodies[k]}')
    tilted = [r for r in former if r['faces'].get('BUMPMAP_LOW / declared / records')]
    print(f'   bodies whose record 0 draws a tilted classic face (BUMPMAP_LOW, declared, records): {len(tilted)};'
          f' declaration from: {dict(collections.Counter(r["declared_by"] for r in tilted))}')
    for r in former:
        if any(k.startswith('BUMPMAP_LOW / not declared') for k in r['faces']):
            print(f'   BUMPMAP_LOW without the declaration: {r["name"]} ({"text" if r["text"] else "binary"} body)')
    guard = [r for r in out if r['guard']]
    print(f'B. bodies the tangent-declaration guard touches (no declaration, a BUMPMAP_LOW classic record and an'
          f' effect material declaring t_BumpTexture): {len(guard)}; baked by the installed record today:'
          f' {sum(1 for r in guard if r["eligible"])}')
    for r in guard:
        print(f'   {r["name"]} eligible={r["eligible"]} refuse={r["refuse"]}')
    today = [r for r in out if r['eligible']]
    print(f'   bodies baked today with a classic record in the table: {len(today)}; of them with a classic record'
          f' drawn by a visible record-0 group: {sum(1 for r in today if r["faces"])}')
    for r in today:            # planned on a classic_view now: part of the byte-identity set (nonfx_bake_check.py)
        print(f'   {r["name"]}')


if __name__ == '__main__':
    main()
