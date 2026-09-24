"""Census before/after the engine texture lookup in lod_atlas (texture-lookup.md sections 9-11, 2026-09-24).

Reads two lod_batch_census.py --out directories (full vanilla body set, bottle X3, read-only) and prints:
refuse= counts per reason and the eligible count before and after; the rows refused texture_unresolved before with
their refuse=/filter=/ELIGIBLE after; every other row whose refuse=, filter= or eligibility changed; and, with
--animation-bodies, the rows of the bodies whose materials carry the texture animations -79 / -81 (scan of the
winning bodies, about 150 s). The census outputs stay in the scratchpad.

    python3 tools/analysis/lod_batch_census.py --out <before> --jobs 8     # at ff70bca8, 133 s
    python3 tools/analysis/lod_batch_census.py --out <after> --jobs 8      # with the resolver, 133 s
    python3 verification/results/lod-overlay-batch/texture_lookup_census_compare.py <before> <after> --animation-bodies \
        > verification/results/lod-overlay-batch/texture_lookup_census_compare_out.txt
"""
import re
import sys
from collections import Counter
from pathlib import Path

GAME = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'


def rows(folder):
    out = {}
    for line in (Path(folder) / 'census.txt').read_text().splitlines():
        name = line.split(' ', 1)[0]
        refuse = re.search(r' refuse=(\S+)', line)
        filt = re.search(r' filter=(\S+)', line)
        anim = re.search(r' anim_groups=(\d+) anim_rows=(\S*) anim_material0=(\d+)', line)
        out[name] = dict(refuse=refuse.group(1) if refuse else '?', filter=filt.group(1) if filt else '?',
                         eligible=line.endswith(' ELIGIBLE') or ' ELIGIBLE ' in line, line=line,
                         anim=anim.groups() if anim else None)
    return out


def fmt(r):
    return f'refuse={r["refuse"]} filter={r["filter"]}' + (' ELIGIBLE' if r['eligible'] else '')


def reasons(rs):
    c = Counter()
    for r in rs.values():
        for x in r['refuse'].split(','):
            if x not in ('-', '?'):
                c[x] += 1
    return c


def animation_bodies(ids):
    """{id: set of lower-case census names} of the winning bodies with a material slot name '-<id>...'."""
    sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
    import bob1
    import body_materials
    import lod_atlas
    import lod_overlay
    assets, _ = lod_overlay.original_assets(GAME)
    out = {i: set() for i in ids}
    for key, entries in list(assets.entries.items()):
        k = key.removeprefix('addon/')
        if not k.startswith('objects/') or not k.endswith(('.bob', '.bod')):
            continue
        try:
            data = assets.read_entry(entries[-1])
            tree = bob1.parse(data, lod_overlay.MAX_TRAILING) if bob1.kind(data) else bob1.parse_text(data)
        except Exception:
            continue
        finally:
            assets.cache.clear()
        for mat in bob1.materials(tree):
            for raw in body_materials.slots(mat).values():
                n = lod_atlas.texture_id(raw if isinstance(raw, bytes) else str(raw).encode())
                if n is not None and -n in out:
                    out[-n].add(k[len('objects/'):].rsplit('.', 1)[0])
    return out


def main(before, after, anim=False):
    b, a = rows(before), rows(after)
    print(f'bodies before {len(b)} after {len(a)}; names only before {sorted(set(b) - set(a))} '
          f'only after {sorted(set(a) - set(b))}')
    rb, ra = reasons(b), reasons(a)
    print('refuse= counts per reason (a row counts once per reason), before -> after:')
    for k in sorted(set(rb) | set(ra)):
        print(f'  {k}: {rb[k]} -> {ra[k]}')
    eb, ea = sum(r['eligible'] for r in b.values()), sum(r['eligible'] for r in a.values())
    print(f'eligible before {eb} after {ea} ({ea - eb:+d}); newly eligible: '
          f'{sorted(n for n in a if a[n]["eligible"] and not b.get(n, {}).get("eligible"))}; no longer eligible: '
          f'{sorted(n for n in b if b[n]["eligible"] and not a.get(n, {}).get("eligible"))}')
    unresolved = [n for n, r in b.items() if 'texture_unresolved' in r['refuse'].split(',')]
    print(f'\ntexture_unresolved before: {len(unresolved)} rows; after: {ra["texture_unresolved"]}')
    for n in unresolved:
        print(f'  {n}: {fmt(b[n])} -> {fmt(a[n])}')
    other = [n for n in b if n in a and n not in unresolved and
             (b[n]['refuse'], b[n]['filter'], b[n]['eligible']) != (a[n]['refuse'], a[n]['filter'], a[n]['eligible'])]
    print(f'\nother rows with a changed refuse/filter/eligibility: {len(other)}')
    for n in other:
        print(f'  {n}: {fmt(b[n])} -> {fmt(a[n])}' + (f' anim={a[n]["anim"]}' if a[n]['anim'] else ''))
    if anim:
        low = {n.lower(): n for n in a}
        for i, names in sorted(animation_bodies((79, 81)).items()):
            found = [low[n.lower()] for n in names if n.lower() in low]
            after_c = Counter(fmt(a[n]) for n in found)
            before_c = Counter(fmt(b[n]) for n in found if n in b)
            print(f'\nbodies with a -{i} material: {len(names)}; census rows {len(found)}')
            print(f'  before: {dict(before_c.most_common())}')
            print(f'  after:  {dict(after_c.most_common())}')
            groups = [a[n]['anim'] for n in found if a[n]['anim']]
            print(f'  after, rows with animated record-0 groups: {len(groups)}, groups {sum(int(g[0]) for g in groups)},'
                  f' on material 0 {sum(int(g[2]) for g in groups)}')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], '--animation-bodies' in sys.argv[3:])
