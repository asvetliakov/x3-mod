#!/usr/bin/env python3
"""Colour-grade census of merged-LOD overlay bodies: does every atlas merged material carry the grade of each
material it absorbs?

Since 2026-11-03 lod_atlas splits the opaque classes by colour grade (lod_atlas.grade_key: colormatrix, Brightness,
contrast, saturation, hue, g_Brightness, g_Contrast, g_Saturation, g_Hue and every g_Color_* parameter, exact parsed
values). Before that a merged material copied its dominant's grade onto every absorbed material (FL run 27: the vent
material of argon_food_S_factory_B tinted the whole station green; docs/verification/farnhams-legacy.md).

For each atlas body of the markers it reads the source material table (the marker's source catalogue in GAME) and
the merged material: the baked one from the overlay catalogue beside the marker when it holds the body, else the
grade of the row's dominant (the merged material is a copy of it). A merged row (widened copies excluded) whose
absorbed materials do not all share its grade is a mismatch. Totals:

  bodies, atlas_bodies, atlas_rows, unread    marker bodies, atlas bodies, atlas merged materials, unreadable bodies
  mismatch_bodies, mismatch_rows              bodies / atlas rows with at least one absorbed material of another grade
  dominant_graded_spread                      mismatch rows whose merged grade has colormatrix 1 (spread to others)
  graded_lost                                 mismatch rows whose merged grade has no colormatrix 1 but a member does
  extra_draws                                 sum over rows of (distinct member grades - 1): the merged materials the
                                              grade split adds to an overlay baked without it (0 on a split bake)
  near_duplicate_rows                         rows with two member grades that declare the same parameters and whose
                                              values all lie within 1/256 relative of each other
  presence_only_rows                          rows with two member grades that agree on every parameter both declare
  extra_draws_colormatrix_family              extra_draws counted on the grade key without its g_Color_* parameters
  split_bodies, extra_per_body, extra_max     bodies with extra_draws > 0, their count per extra ("1": n, ...), the
                                              largest and its body
  merged_materials, merged_materials_split    atlas merged materials of the markers, and of the same bodies split
                                              (merged_materials + extra_draws)
  alpha_mismatch_rows                         non-atlas (alpha) merged rows whose members differ in grade (not split;
                                              the alpha path keeps one group per part)

  X3M_EDITION=fl python3 tools/analysis/lod_grade_census.py --game GAME MARKER.json ...
  X3M_EDITION=fl python3 tools/analysis/lod_grade_census.py --game GAME --candidate OUT     (OUT/addon*/NN.x3m-lod.json)
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bob1  # noqa: E402
import lod_atlas  # noqa: E402

NEAR = 1.0 / 256


def table_only(data):
    """The material table of a binary body, read up to its first MAT section (none: [])."""
    r = bob1.Reader(data)
    r.expect('BOB1')
    while True:
        t = r.tag()
        if t in ('INFO', 'NAME'):
            r.cstr()
        elif t in ('VERS', 'SND1'):
            r.u32()
        elif t in bob1.MATVER:
            return [bob1.read_material(r, bob1.MATVER[t]) for _ in range(r.count())]
        else:
            return []
        r.expect('/' + t[:3])


def body_table(data):
    """Material table of a body (binary fast path, the full parser for anything else)."""
    try:
        return table_only(data)
    except Exception:
        return bob1.materials(bob1.parse(data, max_trailing=64))


def source_table(assets, body):
    """The source body's material table: the member of the catalogue the marker names (the marker's member path when
    that catalogue holds it, else its other extension: a text source .pbd is baked to a .pbb member)."""
    member, source = body['member'].lower(), body['source']
    es = sorted((e for e in bob1.body_members(assets, body['name']) if e['source'] == source),
                key=lambda e: e['path'].lower() != member)
    if not es:
        raise FileNotFoundError(f'{source}:{body["member"]} is not mounted')
    return body_table(assets.read_entry(es[0]))


def overlay_index(folder):
    """{lower-case member path: (dat path, offset, size)} over every NN.cat beside a marker in `folder`."""
    from inspect_x3 import read_catalogue
    index = {}
    for marker in sorted(Path(folder).glob('[0-9][0-9]' + '.x3m-lod.json')):
        cat = marker.with_name(marker.name.split('.')[0] + '.cat')
        if cat.is_file():
            for e in read_catalogue(cat):
                key = e['path'].replace('\\', '/').lower()
                index.setdefault(key, (cat.with_suffix('.dat'), e['offset'], e['size']))
    return index


def overlay_table(index, body):
    """The baked body's material table from the overlay catalogue (None when no overlay catalogue holds it)."""
    hit = index.get(body['member'].replace('\\', '/').lower())
    if hit is None:
        return None
    dat, offset, size = hit
    with open(dat, 'rb') as stream:
        stream.seek(offset)
        raw = bytes(v ^ 0x33 for v in stream.read(size))
    from sector_fog_census import unpack
    return body_table(unpack(raw))


def family(key):
    """A grade key without its g_Color_* parameters (the colormatrix / Brightness / contrast / saturation / hue set)."""
    return tuple(p for p in key if not p[0].startswith(lod_atlas.GRADE_PREFIX))


def colormatrix_on(key):
    return any(n == b'colormatrix' and v and v[0] for n, _, v in key)


def near(a, b):
    """Two grade keys declaring the same parameters whose values all lie within NEAR relative."""
    if [(n, t, len(v)) for n, t, v in a] != [(n, t, len(v)) for n, t, v in b]:
        return False
    return all(abs(x - y) <= NEAR * max(abs(x), abs(y), 65536) for (_, _, va), (_, _, vb) in zip(a, b)
               for x, y in zip(va, vb))


def presence_only(a, b):
    da, db = {(n, t): v for n, t, v in a}, {(n, t): v for n, t, v in b}
    return da != db and all(da[k] == db[k] for k in set(da) & set(db))


def census(assets, markers, out=print):
    tot = dict(bodies=0, atlas_bodies=0, atlas_rows=0, unread=0, mismatch_bodies=0, mismatch_rows=0,
               dominant_graded_spread=0, graded_lost=0, extra_draws=0, near_duplicate_rows=0, presence_only_rows=0,
               alpha_mismatch_rows=0, extra_draws_colormatrix_family=0, split_bodies=0, extra_per_body={},
               extra_max=[0, None])
    for marker in markers:
        marker = Path(marker)
        index = overlay_index(marker.parent)
        tag = marker.name[:2]
        for b in json.loads(marker.read_text())['bodies']:
            tot['bodies'] += 1
            if b.get('collapse') != 'atlas' or not b.get('atlas'):
                continue
            tot['atlas_bodies'] += 1
            try:
                mats = source_table(assets, b)
                baked = overlay_table(index, b)
            except Exception as ex:                      # noqa: BLE001 (counted and named, never fatal)
                tot['unread'] += 1
                out(f'UNREAD {tag} {b["name"]} {ex!r:.100}')
                continue
            grade = lambda i: lod_atlas.grade_key(mats[i])
            body_mismatch, body_extra = False, 0
            for s in b.get('synth', []):
                members = sorted(set(s['absorbed']) | {s['dominant']})
                if not s.get('atlas'):
                    if len({grade(m) for m in members}) > 1:
                        tot['alpha_mismatch_rows'] += 1
                    continue
                if s.get('widened'):
                    continue
                tot['atlas_rows'] += 1
                merged = lod_atlas.grade_key(baked[s['index']]) if baked is not None else grade(s['dominant'])
                keys = sorted({grade(m) for m in s['absorbed']})
                tot['extra_draws'] += len(keys) - 1
                body_extra += len(keys) - 1
                tot['extra_draws_colormatrix_family'] += len({family(k) for k in keys}) - 1
                pairs = [(x, y) for i, x in enumerate(keys) for y in keys[i + 1:]]
                tot['near_duplicate_rows'] += any(near(x, y) for x, y in pairs)
                tot['presence_only_rows'] += any(presence_only(x, y) for x, y in pairs)
                wrong = [m for m in s['absorbed'] if grade(m) != merged]
                if not wrong:
                    continue
                tot['mismatch_rows'] += 1
                body_mismatch = True
                spread = colormatrix_on(merged)
                tot['dominant_graded_spread' if spread else 'graded_lost'] += \
                    spread or any(colormatrix_on(grade(m)) for m in wrong)
                out(f'MISMATCH {tag} {b["name"]} merged={s["index"]} dominant={s["dominant"]} '
                    f'grades={len(keys)} wrong={wrong} source={"baked" if baked is not None else "dominant"}')
            tot['mismatch_bodies'] += body_mismatch
            if body_extra:
                tot['split_bodies'] += 1
                hist = tot['extra_per_body']
                hist[str(body_extra)] = hist.get(str(body_extra), 0) + 1
                if body_extra > tot['extra_max'][0]:
                    tot['extra_max'] = [body_extra, b['name']]
    tot['merged_materials'] = tot['atlas_rows']                       # atlas merged materials of the markers
    tot['merged_materials_split'] = tot['atlas_rows'] + tot['extra_draws']   # the same bodies baked with the split
    return tot


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--game', type=Path, required=True, help='game directory holding the source catalogues')
    ap.add_argument('--edition', choices=('auto', 'ap', 'fl'), default='auto')
    ap.add_argument('--candidate', type=Path, metavar='OUT',
                    help='baked candidate directory (OUT/addon*/NN.x3m-lod.json)')
    ap.add_argument('markers', nargs='*', type=Path, help='x3m-lod marker JSON files')
    a = ap.parse_args(argv)
    markers = list(a.markers)
    if a.candidate:
        markers += sorted(a.candidate.glob('addon*/[0-9][0-9].x3m-lod.json'))
    if not markers:
        ap.error('name marker files or --candidate OUT')
    sfc = bob1._archive_modules()
    assets = sfc.Assets(a.game, edition=None if a.edition == 'auto' else a.edition)
    tot = census(assets, markers)
    print('TOTAL ' + json.dumps(tot, sort_keys=True))
    return 0


if __name__ == '__main__':
    sys.exit(main())
