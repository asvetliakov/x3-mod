#!/usr/bin/env python3
"""Order of the per-group 7-int records: shipped convention, and whether first-use ordering
changes the collapsed records (read-only; counts and hashes only).

A. Every group of every record of the named bodies (shipped, overlay skipped): do its
   records list exactly the distinct points of its faces, in the order the faces first use
   them? B. For --collapse glow, glow-area 70, two and one: the coarse record C built by
   lod_overlay.coarse_record (first-use records) against the same record with the previous
   rule (records of the merged groups concatenated in group order): equal bytes or not.
   C. For an atlas overlay root (optional): the atlas records of its C against a previous
   root's (sha256 of the record bytes per body).

  python3 verification/results/lod-overlay-pilot/record_order.py <body> ... [--new ROOT --old ROOT]
"""
import hashlib
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import bob1  # noqa: E402
import lod_overlay  # noqa: E402
from inspect_x3 import read_catalogue  # noqa: E402
from sector_fog_census import unpack  # noqa: E402


def record_bytes(lod):
    w = bob1.Writer()
    bob1.write_lod(w, dict(lod, value=0))
    return bytes(w.b)


def concatenated(lod):
    out = dict(lod, parts=[])
    for part in lod['parts']:
        out['parts'].append(dict(part, groups=[dict(g, extra=g['extra']) for g in part['groups']]))
    return out


def old_rule(coarsest, collapse, alpha, kept, remap):
    """C with the pre-fix merged_group (records concatenated in group order)."""
    real = lod_overlay.first_use_records
    lod_overlay.first_use_records = lambda groups, faces: [e for g in groups for e in g['extra']]
    try:
        return lod_overlay.coarse_record(coarsest, None, alpha, collapse, kept, remap)
    finally:
        lod_overlay.first_use_records = real


def atlas_c(root):
    (cat,) = sorted(Path(root).glob('addon/[0-9][0-9].cat'))
    raw = cat.with_suffix('.dat').read_bytes()
    marker = json.loads(cat.with_name(cat.stem + lod_overlay.MARKER_SUFFIX).read_text())
    new_lod = {b['member']: b['new_lod'] for b in marker['bodies']}
    out = {}
    for e in read_catalogue(cat):
        if e['path'] in new_lod:
            data = unpack(bytes(v ^ 0x33 for v in raw[e['offset']:e['offset'] + e['size']]))
            out[e['path']] = hashlib.sha256(record_bytes(bob1.lods(bob1.parse(data))[new_lod[e['path']]])).hexdigest()
    return out


def main(argv):
    roots = {}
    while '--new' in argv or '--old' in argv:
        k = next(i for i, a in enumerate(argv) if a in ('--new', '--old'))
        roots[argv[k][2:]] = argv[k + 1]
        del argv[k:k + 2]
    assets, skipped = lod_overlay.original_assets(bob1.DEFAULT_GAME)
    print(f'sources read without {skipped}')
    groups = ordered = exact = 0
    for body in argv:
        tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, body)))
        for lod in bob1.lods(tree):
            for part in lod['parts']:
                for g in part['groups']:
                    if 'extra' not in g:
                        continue
                    groups += 1
                    first = list(dict.fromkeys(i for f in g['faces'] for i in f[:3]))
                    idx = [e[0] for e in g['extra']]
                    ordered += idx == first
                    exact += sorted(idx) == sorted(first)
    print(f'A shipped groups with records {groups}: first-use order {ordered}, same point set {exact}')
    for body in argv:
        tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, body)))
        mats, ladder = bob1.materials(tree), bob1.lods(tree)
        alpha = lod_overlay.alpha_materials(mats)
        used = sorted({g['material'] for p in ladder[-1]['parts'] for g in p['groups']})
        glow = lod_overlay.glow_materials(assets, mats, used)
        area = lod_overlay.light_area_materials(assets, mats, ladder[-1], glow, 70.0)[0]
        row = []
        for collapse, kept in (('glow', glow), ('glow-area', glow | area), ('two', set()), ('one', set())):
            m = list(mats)
            remap, _ = lod_overlay.synth_materials(m, ladder[-1], alpha, collapse, kept)
            new = lod_overlay.coarse_record(ladder[-1], None, alpha, collapse, kept, remap)
            old = old_rule(ladder[-1], collapse, alpha, kept, remap)
            row.append(f'{collapse}={"same" if record_bytes(new) == record_bytes(old) else "CHANGED"}')
        print(f'B {body}: C record bytes, first-use vs concatenated records: {" ".join(row)}')
    if roots:
        new, old = atlas_c(roots['new']), atlas_c(roots['old'])
        for member in sorted(new):
            print(f'C atlas {member}: C record sha256 {new[member][:16]} vs previous {old.get(member, "-")[:16]}'
                  f' {"same" if new[member] == old.get(member) else "CHANGED"}')


if __name__ == '__main__':
    main(sys.argv[1:])
