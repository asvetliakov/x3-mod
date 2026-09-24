"""Census before/after the alpha-rule fix (lod_overlay.alpha_materials: a flagged material is alpha only when its
alpha can drop below 1 or its blend is not source-over; 2026-09-24). Reads two lod_batch_census.py --out
directories (full vanilla body set, bottle X3, read-only) and prints: rows whose alpha material count (alpha_mats=,
materials of record 0 in the alpha set) shrinks, split by eligibility; eligibility changes; per eligible-in-both
body the atlas side at 1920 and the census atlas MB estimate, with totals; the Terran rows.

    python3 tools/analysis/lod_batch_census.py --out <before> --jobs 8     # at 1266cf4e, 145 s
    python3 tools/analysis/lod_batch_census.py --out <after> --jobs 8      # with the fix, 146 s
    python3 verification/results/lod-overlay-batch/alpha_rule_census_compare.py <before> <after> \
        > verification/results/lod-overlay-batch/alpha_rule_census_compare_out.txt
"""
import re
import sys
from collections import Counter
from pathlib import Path


def rows(folder):
    out = {}
    for line in (Path(folder) / 'census.txt').read_text().splitlines():
        name = line.split(' ', 1)[0]
        g = lambda pat: (m.group(1) if (m := re.search(pat, line)) else None)
        atlas = re.search(r' atlas@1920=(\d+)\(ratio ([\d.]+), [^)]*?([\d.]+) MB\)', line)
        out[name] = dict(alpha=int(g(r' alpha_mats=(\d+)') or 0), refuse=g(r' refuse=(\S+)'),
                         eligible=line.endswith(' ELIGIBLE') or ' ELIGIBLE ' in line,
                         atlas=int(atlas.group(1)) if atlas else None, ratio=float(atlas.group(2)) if atlas else None,
                         mb=float(atlas.group(3)) if atlas else 0.0, member=float(g(r' member~([\d.]+) MB') or 0),
                         drawn=int(g(r' C_drawn=(\d+)') or 0))
    return out


def main(before, after):
    b, a = rows(before), rows(after)
    names = sorted(set(b) & set(a))
    shrink = [n for n in names if a[n]['alpha'] < b[n]['alpha']]
    grow = [n for n in names if a[n]['alpha'] > b[n]['alpha']]
    print(f'rows {len(names)}; alpha_mats shrinks {len(shrink)} (eligible after {sum(a[n]["eligible"] for n in shrink)},'
          f' eligible before and after {sum(a[n]["eligible"] and b[n]["eligible"] for n in shrink)}); grows {len(grow)}')
    print('shrink by refuse= after:', dict(Counter(a[n]['refuse'] for n in shrink)))
    print('newly eligible:', [n for n in names if a[n]['eligible'] and not b[n]['eligible']])
    print('no longer eligible:', [(n, a[n]['refuse']) for n in names if b[n]['eligible'] and not a[n]['eligible']])
    both = [n for n in names if a[n]['eligible'] and b[n]['eligible']]
    sides = Counter((b[n]['atlas'], a[n]['atlas']) for n in both)
    print(f'eligible in both {len(both)}: atlas side at 1920 before -> after {dict(sides)}')
    for n in both:
        if b[n]['atlas'] != a[n]['atlas']:
            print(f'  side change {n}: {b[n]["atlas"]} -> {a[n]["atlas"]} ratio {b[n]["ratio"]} -> {a[n]["ratio"]}')
    tb, ta = sum(b[n]['mb'] for n in both), sum(a[n]['mb'] for n in both)
    mb, ma = sum(b[n]['member'] for n in both), sum(a[n]['member'] for n in both)
    print(f'census atlas MB (eligible in both) {tb:.2f} -> {ta:.2f} ({ta - tb:+.2f}); member~ MB {mb:.2f} -> {ma:.2f}')
    db, da = sum(b[n]['drawn'] for n in both), sum(a[n]['drawn'] for n in both)
    print(f'C_drawn summed (eligible in both) {db} -> {da}; bodies with more draws'
          f' {sum(a[n]["drawn"] > b[n]["drawn"] for n in both)}, fewer {sum(a[n]["drawn"] < b[n]["drawn"] for n in both)}')
    eb, ea = sum(b[n]['mb'] for n in names if b[n]['eligible']), sum(a[n]['mb'] for n in names if a[n]['eligible'])
    print(f'census atlas MB (all eligible) {eb:.2f} -> {ea:.2f} ({ea - eb:+.2f})')
    ratio_drop = sorted((a[n]['ratio'] / b[n]['ratio'], n) for n in shrink if n in both and b[n]['ratio'])
    print(f'texels/px at 1920 on the shrinking eligible rows: min after {min((a[n]["ratio"] for n in shrink if n in both), default=None)};'
          f' largest drops {[(n.rsplit("/", 1)[-1], round(r, 3)) for r, n in ratio_drop[:5]]}')
    for n in names:
        if 'terran' in n.lower() and (a[n]['alpha'] != b[n]['alpha'] or a[n]['eligible'] != b[n]['eligible']):
            print(f'  {n}: alpha_mats {b[n]["alpha"]} -> {a[n]["alpha"]} eligible {b[n]["eligible"]} -> {a[n]["eligible"]}'
                  f' atlas {b[n]["atlas"]} -> {a[n]["atlas"]} ({b[n]["mb"]} -> {a[n]["mb"]} MB) refuse {a[n]["refuse"]}')


if __name__ == '__main__':
    main(*sys.argv[1:3])
