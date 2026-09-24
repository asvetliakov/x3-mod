"""Before/after the alpha-rule fix (lod_overlay.alpha_materials, 2026-09-24): per body of two scratch
`lod_overlay.py --batch --only <list> --out <dir>` bakes, the coarse record's groups (kind:material:faces from the
bake log), draws, atlas side, texels per pixel, atlas and member bytes, and the totals.

    python3 alpha_rule_bake_compare.py <before dir> <after dir> > alpha_rule_bake_compare.txt
"""
import json
import re
import sys


def load(folder):
    bodies = {b['name']: b for b in json.load(open(f'{folder}/x3m-lod-batch.json'))['bodies'] if b.get('eligible')}
    groups = {}
    name = None
    for line in open(f'{folder}/x3m-lod-batch-bodies.txt'):
        if not line.startswith(' '):
            name = line.split(':', 1)[0]
        m = re.search(r" collapse=atlas groups=(\[.*?\])", line)
        if m and name:
            groups[name] = m.group(1)
    return bodies, groups


def main(bdir, adir):
    (b, bg), (a, ag) = load(bdir), load(adir)
    tot = [0, 0, 0, 0, 0, 0]
    for n in sorted(set(b) | set(a)):
        x, y = b.get(n, {}), a.get(n, {})
        print(f'{n.rsplit("/", 1)[-1]}: draws {x.get("draws")} -> {y.get("draws")}; atlas {x.get("atlas_size")} -> '
              f'{y.get("atlas_size")}; ratio {x.get("ratio")} -> {y.get("ratio")}; atlas_bytes {x.get("atlas_bytes")} -> '
              f'{y.get("atlas_bytes")}; member_bytes {x.get("member_bytes")} -> {y.get("member_bytes")}')
        print(f'    before {bg.get(n)}')
        print(f'    after  {ag.get(n)}')
        for i, k in enumerate(('atlas_bytes', 'member_bytes')):
            tot[2 * i] += x.get(k) or 0
            tot[2 * i + 1] += y.get(k) or 0
    print(f'total atlas_bytes {tot[0]} -> {tot[1]} ({tot[1] - tot[0]:+d}); member_bytes {tot[2]} -> {tot[3]}'
          f' ({tot[3] - tot[2]:+d}); bodies {len(b)} -> {len(a)}')


if __name__ == '__main__':
    main(*sys.argv[1:3])
