#!/usr/bin/env python3
"""Run 74 A (run277): original LOD 0 vs installed merged record of one body.
Prints per-material face counts and model-space bboxes (LOD 0), and the installed ladder with
per-group material / face counts and, for the merged alpha group, which source materials its
faces came from (matched by point position+uv triple against LOD 0 faces).
Usage: body_records.py BODY   (reads the installed game; no Wine)."""
import sys, collections
sys.path.insert(0, '/Users/asvetl/x3-mod/tools/analysis')
import bob1, lod_overlay
name = sys.argv[1]
game = bob1.DEFAULT_GAME
oa, skipped = lod_overlay.original_assets(game)
orig = bob1.parse(oa.read_entry(bob1.resolve_body(oa, name)))
ia = lod_overlay.Assets(__import__('pathlib').Path(game))
ie = bob1.resolve_body(ia, name)
inst = bob1.parse(ia.read_entry(ie))
print('installed entry', ie['source'], ie['path'], 'skipped for original', skipped)
mats = bob1.materials(orig); alpha = lod_overlay.alpha_materials(mats)
L0 = bob1.lods(orig)[0]; P = L0['points']
print('orig ladder', [(l['value'], sum(len(p['groups']) for p in l['parts'])) for l in bob1.lods(orig)])
print('inst ladder', [(l['value'], sum(len(p['groups']) for p in l['parts'])) for l in bob1.lods(inst)])
key_of = {}
for pi, part in enumerate(L0['parts']):
    for g in part['groups']:
        pts = [P[i] for f in g['faces'] for i in f[:3]]
        xs = [p[1] for p in pts]; ys = [p[2] for p in pts]; zs = [p[3] for p in pts]
        print(f'L0 part{pi} flags={part["flags"]:#x} mat{g["material"]:>2} {"A" if g["material"] in alpha else "-"} faces={len(g["faces"]):5d} '
              f'x=[{min(xs)},{max(xs)}] y=[{min(ys)},{max(ys)}] z=[{min(zs)},{max(zs)}]')
        for f in g['faces']:
            key_of[tuple(sorted(tuple(P[i][1:4]) for i in f[:3]))] = g['material']
for li, rec in enumerate(bob1.lods(inst)):
    Q = rec['points']
    for pi, part in enumerate(rec['parts']):
        for g in part['groups']:
            src = collections.Counter(key_of.get(tuple(sorted(tuple(Q[i][1:4]) for i in f[:3])), 'unmatched') for f in g['faces'])
            desc = ' '.join(f'mat{k}:{v}' for k, v in src.most_common(12)) if li == 1 else ''
            print(f'inst rec{li} value={rec["value"]} part{pi} flags={part["flags"]:#x} mat{g["material"]} faces={len(g["faces"])} {desc}')
imats = bob1.materials(inst)
print('inst materials', len(imats), 'orig', len(mats))
