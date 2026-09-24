"""Self-check of a scratch bake of stations/x3tc/terran_spp_panel with the lod_recipes weld (design
docs/architecture/lattice-baker-fix.md section 5, host check 2). Read-only: the scratch overlay named on the
command line, the installed overlay member (install-fleet3, before) and the vanilla catalogues through the
baker's reader. Prints derived numbers only.

  python3 verification/results/lattice-baker-fix/bake_check.py <scratch --out dir>
"""
import gzip
import json
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, 'tools/analysis')
import bob1, lod_overlay, lod_recipes  # noqa: E401,E402
from inspect_x3 import read_catalogue  # noqa: E402

NAME, MEMBER, PANE, PLANE = 'stations/x3tc/terran_spp_panel', 'objects/stations/x3tc/terran_spp_panel.pbb', 21, 105


def member(cat):
    e = {x['path']: x for x in read_catalogue(cat)}.get(MEMBER)
    if e is None:
        return None
    with cat.with_suffix('.dat').open('rb') as f:
        f.seek(e['offset'])
        raw = bytes(v ^ 0x33 for v in f.read(e['size']))
    return bob1.parse(gzip.decompress(raw) if raw[:2] == b'\x1f\x8b' else raw)


def counts(lod):
    return dict(points=len(lod['points']), faces=sum(len(g['faces']) for p in lod['parts'] for g in p['groups']),
                groups=lod_overlay.drawn_groups(lod))


def index_components(lod, material):
    faces = lod_recipes.material_faces(lod, material)
    parent = {}

    def find(a):
        while parent.setdefault(a, a) != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    for f in faces:
        for i in f[1:3]:
            ra, rb = find(f[0]), find(i)
            if ra != rb:
                parent[rb] = ra
    comps = defaultdict(set)
    for f in faces:
        for i in f[:3]:
            comps[find(i)].add(i)
    return list(comps.values())


def main():
    out = Path(sys.argv[1])
    game = bob1.DEFAULT_GAME
    ok = True
    cats = sorted((out / 'addon').glob('*.cat'))
    after = next(t for t in (member(c) for c in cats) if t is not None)
    marker = next(b for c in cats for b in json.loads(c.with_suffix('.x3m-lod.json').read_text())['bodies']
                  if b['name'] == NAME)
    assets, _ = lod_overlay.original_assets(game)
    vanilla = bob1.lods(bob1.parse(assets.read_entry(bob1.resolve_body(assets, NAME)), lod_overlay.MAX_TRAILING))
    installed = None
    for m in sorted((game / 'addon').glob('*.x3m-lod.json')):
        if any(b['name'] == NAME for b in json.loads(m.read_text()).get('bodies', ())):
            installed = member(m.with_name(m.name.split('.')[0] + '.cat'))
            print(f'installed overlay member: addon/{m.name.split(".")[0]}.cat')
    L = bob1.lods(after)
    c, pad = L[1], L[2]
    print('ladder after', [l['value'] for l in L], 'source_record', marker['source_record'],
          'recipe', (marker.get('recipe') or {}).get('recipe'), 'draws (marker)', marker['draws'])
    for label, lod in (('vanilla record 0', vanilla[0]), ('vanilla record 1', vanilla[1]),
                       ('installed C (fleet3, from record 0)', bob1.lods(installed)[1] if installed else None),
                       ('scratch C (recipe)', c)):
        if lod is not None:
            print(f'  {label}: {counts(lod)}')
    pane_pts = {i for f in lod_recipes.material_faces(c, PANE) for i in f[:3]}
    ys = {c['points'][i][2] for i in pane_pts}
    comps = index_components(c, PANE)
    planar = ys == {PLANE}
    below = sum(1 for f in lod_recipes.material_faces(c, PANE) if min(c['points'][i][2] for i in f[:3]) < PLANE - 0.5)
    print(f'pane (material {PANE}) in C: faces {len(lod_recipes.material_faces(c, PANE))}, points {len(pane_pts)},'
          f' strips {len(comps)}, y values {sorted(ys)}, faces below y {PLANE} - 0.5: {below}')
    # abutment: the x edges of neighbouring strips in a row coincide exactly
    edges = defaultdict(list)
    for s in comps:
        xs = [c['points'][i][1] for i in s]
        zs = [c['points'][i][3] for i in s]
        edges[round((min(zs) + max(zs)) / 2, -2)].append((min(xs), max(xs)))
    shared, gaps = 0, []
    for row in edges.values():
        row.sort()
        for a, b in zip(row, row[1:]):
            if a[1] == b[0]:
                shared += 1
            else:
                gaps.append(b[0] - a[1])
    widths = [hi - lo for row in edges.values() for lo, hi in row]
    print(f'  strip widths {min(widths)}..{max(widths)}; bit-identical shared edges {shared};'
          f' other neighbour steps (segment gaps) {len(gaps)}, min {min(gaps) if gaps else None}')
    poke = lod_recipes.above_share(c, PANE)
    poke_v = lod_recipes.above_share(vanilla[1], PANE)
    print(f'poke-through over the pane footprint (20-unit cells): vanilla record 1 {poke_v["above"]}/{poke_v["cells"]}'
          f' = {poke_v["share"]:.4f} (max {poke_v["max_above"]:.1f}); scratch C {poke["above"]}/{poke["cells"]}'
          f' = {poke["share"]:.4f} (max {poke["max_above"]:.1f})')
    split = (marker.get('atlas') or {}).get('split_groups')
    pad_same = dict(pad, value=vanilla[3]['value']) == vanilla[3]
    widest = max(len({i for f in g['faces'] for i in f[:3]}) for p in c['parts'] for g in p['groups'])
    print(f'drawn groups of C {lod_overlay.drawn_groups(c)}; split_groups {split}; widest group points {widest};'
          f' pad record (index 2) equals vanilla record 3 except its threshold: {pad_same}')
    checks = dict(planar=planar, no_face_below=below == 0, strips_132=len(comps) == 132,
                  no_poke=poke['above'] == 0, draws_3=lod_overlay.drawn_groups(c) == 3 and marker['draws'] == 3,
                  no_split=not split, under_60000=widest <= 60000, pad_vanilla=pad_same)
    ok = all(checks.values())
    print('checks', checks, '->', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
