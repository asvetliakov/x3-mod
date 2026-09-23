#!/usr/bin/env python3
"""Structural diff of every vanilla text body (.pbd) against the game's compiled binary (.pbb) of the
same stem (read-only; nothing is written anywhere).

  PYTHONPATH=tools/analysis python3 text_pairs.py [GAME]

For each (text member, binary member) of a stem that has both: bob1.parse_text(text) against
bob1.parse(binary). Ordered comparison per record: value (threshold), LOD flags, point count, the
exact point fields (flags, position, uv, smoothing group), normals within NTOL 16.16 units, faces,
group materials, part flags without 0x10000000 (the text parser sets it only with PART_VALUES_RAW),
and the material table. 'geometry' is the order-insensitive check: faces as (material, rotated
corner triple of (position, uv, smoothing)) multisets per record. Classes:
  equal       every ordered field matches (normals within NTOL)
  geometry    same faces as a multiset, point order or normals differ
  scaled      every binary face is found in the text record of the same index after scaling it to
              max |position| 65536 (positions within SCALE_TOL); thresholds or flags may differ
  scaled_reordered  the same, but the binary records map to text records in another order
  revision    anything else. For a revision pair the line adds record_map: per binary record, the
              text record whose scaled faces match most of it (text index, matched, binary faces),
              which shows reordered or rebuilt ladders
Every line gives the ordered differences (point count, part flags, normals over NTOL; a
normal_max near 131072 is a flipped normal).
"""
import collections
import sys
from pathlib import Path

import bob1
import sector_fog_census as sfc

NTOL = 4
SCALE_TOL = 3


def scaled_match(x, y):
    """Faces of text record x that match a face of binary record y after scaling x's positions
    to max |position| 65536: same material and (uv, smoothing) corners, positions within
    SCALE_TOL. Returns (matched, faces of x)."""
    mx = max((max(abs(q) for q in p[1:4]) for p in x['points']), default=0) or 1
    k = 65536 / mx

    def faces(lod, scale):
        for p in lod['parts']:
            for g in p['groups']:
                for f in g['faces']:
                    cs = [lod['points'][i] for i in f[:3]]
                    keys = [(c[4:6] if c[0] & 2 else (), c[-1]) for c in cs]
                    r = min(range(3), key=lambda j: (keys[j:] + keys[:j], j))
                    cs = cs[r:] + cs[:r]
                    yield (g['material'], tuple(keys[r:] + keys[:r])), [tuple(q * scale for q in c[1:4]) for c in cs]
    pool = collections.defaultdict(list)
    for key, pos in faces(y, 1):
        pool[key].append(pos)
    matched = total = 0
    for key, pos in faces(x, k):
        total += 1
        cand = pool.get(key, [])
        for i, other in enumerate(cand):
            if all(abs(a - b) <= SCALE_TOL for p, q in zip(pos, other) for a, b in zip(p, q)):
                matched += 1
                cand.pop(i)
                break
    return matched, total


def corner_key(c):
    return (c[1:4], c[4:6] if c[0] & 2 else (), c[-1])


def face_multiset(lod):
    out = collections.Counter()
    for p in lod['parts']:
        for g in p['groups']:
            for f in g['faces']:
                ks = [corner_key(lod['points'][i]) for i in f[:3]]
                r = min(range(3), key=lambda k: ks[k:] + ks[:k])
                out[(g['material'], tuple(ks[r:] + ks[:r]))] += 1
    return out


def compare(tt, bt):
    tl, bl = bob1.lods(tt), bob1.lods(bt)
    diffs = collections.Counter()
    nmax = 0
    if len(tl) != len(bl):
        diffs['records'] += 1
    geometry = len(tl) == len(bl)
    for x, y in zip(tl, bl):
        diffs['value'] += x['value'] != y['value']
        diffs['lod_flags'] += x['flags'] != y['flags']
        if len(x['points']) != len(y['points']):
            diffs['point_count'] += 1
        for p, q in zip(x['points'], y['points']):
            if p[0] != q[0] or p[1:6] != q[1:6] or p[-1] != q[-1]:
                diffs['point_fields'] += 1
            elif p[0] & 8:
                d = max(abs(a - b) for a, b in zip(p[6:9], q[6:9]))
                nmax = max(nmax, d)
                diffs['normal_over_tol'] += d > NTOL
        xf = [f for p in x['parts'] for g in p['groups'] for f in g['faces']]
        yf = [f for p in y['parts'] for g in p['groups'] for f in g['faces']]
        diffs['faces'] += sum(1 for a, b in zip(xf, yf) if a != b) + abs(len(xf) - len(yf))
        diffs['group_materials'] += ([[g['material'] for g in p['groups']] for p in x['parts']]
                                     != [[g['material'] for g in p['groups']] for p in y['parts']])
        diffs['part_flags'] += ([p['flags'] & ~bob1.PART_PRECOMPUTED for p in x['parts']]
                                != [p['flags'] & ~bob1.PART_PRECOMPUTED for p in y['parts']])
        geometry = geometry and face_multiset(x) == face_multiset(y)
    tm, bm = bob1.materials(tt), bob1.materials(bt)
    mat_keys = sorted({k for m, n in zip(tm, bm) for k in set(m) | set(n) if m.get(k) != n.get(k)})
    if len(tm) != len(bm):
        mat_keys.append('count')
    ordered = {k: v for k, v in diffs.items() if v}
    cls = 'equal' if not ordered else 'geometry' if geometry else 'revision'
    record_map = None
    if cls == 'revision':
        nf = lambda lod: sum(len(g['faces']) for p in lod['parts'] for g in p['groups'])
        record_map = []
        for y in bl:
            best = max(((j, scaled_match(x, y)[0]) for j, x in enumerate(tl)), key=lambda t: t[1])
            record_map.append((best[0], best[1], nf(y)))
        if all(m == n for _, m, n in record_map):                 # every binary face found after scaling
            cls = 'scaled' if [j for j, _, _ in record_map] == list(range(len(tl))) else 'scaled_reordered'
    return cls, ordered, mat_keys, nmax, record_map


def main():
    game = Path(sys.argv[1]) if len(sys.argv) > 1 else bob1.DEFAULT_GAME
    a = sfc.Assets(game)
    text, binary = {}, {}
    for v in a.entries.values():
        for e in v:
            p = e['path'].lower()
            stem = sfc.canonical(p).removeprefix('addon/')[:-4]
            (text if p.endswith(('.pbd', '.bod')) else binary if p.endswith(('.pbb', '.bob')) else {}).setdefault(
                stem, []).append(e)
    stems = sorted(set(text) & set(binary))
    classes = collections.Counter()
    print(f'game {game}: {len(stems)} stems with a text and a binary member')
    for stem in stems:
        for te in text[stem]:
            td = a.read_entry(te)
            if bob1.text_kind(td) == 'scene':
                classes['scene'] += 1
                print(f'{stem} {te["source"]}: text scene (VER:/P lines), not a body')
                continue
            try:
                tt = bob1.parse_text(td)
            except bob1.FormatError as exc:
                classes['text_parse_error'] += 1
                print(f'{stem} {te["source"]}: text_parse_error {exc}')
                continue
            for be in binary[stem]:
                bd = a.read_entry(be)
                if bob1.kind(bd) != 'BOB1':
                    print(f'{stem} {be["source"]}: binary is {bob1.kind(bd)}, skipped')
                    continue
                cls, diffs, mats, nmax, record_map = compare(tt, bob1.parse(bd, 8))
                classes[cls] += 1
                shape = [(l['value'], len(l['points']), sum(len(g['faces']) for p in l['parts'] for g in p['groups']))
                         for l in bob1.lods(tt)]
                print(f'{stem} text {te["source"]} bin {be["source"]}: {cls} records {len(shape)}'
                      f' (value, points, faces) {shape[:4]} normal_max {nmax}'
                      f' ordered_diffs {diffs or "-"} material_diff_keys {mats or "-"}'
                      + (f' record_map {record_map}' if record_map else ''))
            a.cache.clear()
    print('summary', dict(sorted(classes.items())))


if __name__ == '__main__':
    main()
