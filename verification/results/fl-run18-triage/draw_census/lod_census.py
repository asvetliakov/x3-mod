#!/usr/bin/env python3
"""LOD structure of ship/station bodies: FL (root + addon2, as X3FL.exe mounts) vs stock AP (X3AP-orig, root +
addon/01..04) vs the installed modded AP (/mnt/c/X3, Mayhem + x3m-lod overlay; named bodies only).
Read-only; prints counts, thresholds and names only (no game bytes).

  python3 -I verification/results/fl-run18-triage/draw_census/lod_census.py > verification/results/fl-run18-triage/draw_census/lod_census_out.txt
"""
import collections
import re
import statistics as st
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import bob1  # noqa: E402
import catalogue_mounts  # noqa: E402
from sector_fog_census import Assets, STOCK_AP_CATALOGUES  # noqa: E402

FL, AP, APM = Path('/mnt/c/X3FL'), Path('/mnt/c/X3AP-orig'), Path('/mnt/c/X3')
# bodies matched to the run-18 capture frame 15972 nodes (hull_fingerprint_out.txt; draws, drawn LOD, bbox px)
SEEN = [('ships/argon/argon_m1', 'M1 capital', 32, 0, '801x392'), ('ships/argon/argon_tl', 'TL', 31, 0, '103x108'),
        ('stations/station_scenes/tech/argon_tech_s_factory_a', 'station part', 15, 2, '135x116'),
        ('ships/m7/argon_m7', 'M7', 16, 0, '76x46'), ('ships/boron/boron_ts_liquidstorage_l', 'TS part', 21, 1, '~20')]


def ladder(assets, stem):
    e = bob1.resolve_body(assets, stem)
    data = assets.read_entry(e)
    if data[:4] == b'BOB1':
        tree = bob1.parse(data, None)
    elif e['path'].lower().endswith(('.bod', '.pbd')) and not re.search(rb'^P \d+;', data[:4096], re.M):
        tree = bob1.parse_text(data)
    else:
        return None
    ls = bob1.lods(tree)
    return dict(src=e['source'], n=len(ls), thr=[l['value'] for l in ls[1:]],
                draws=[sum(len(p['groups']) for p in l['parts']) for l in ls])


def stems(assets):
    out = set()
    for k in assets.entries:
        k2 = k.removeprefix('addon/')
        if k2.startswith(('objects/ships/', 'objects/stations/')) and k2.endswith(('.bob', '.bod')) \
                and not re.search(r'-l\d{3}\.bo[bd]$', k2) and '_scene' not in k2:
            out.add(k2[:-4])
    return out


def census(assets):
    res = {}
    for s in sorted(stems(assets)):
        try:
            r = ladder(assets, s)
        except Exception:
            r = None
        if r:
            res[s] = r
    return res


def summarise(name, res):
    for cls in ('objects/ships/', 'objects/stations/'):
        rs = [r for s, r in res.items() if s.startswith(cls)]
        nl = collections.Counter(r['n'] for r in rs)
        d0 = [r['draws'][0] for r in rs if r['draws'][0] > 0]
        multi = [r for r in rs if r['n'] > 1]
        coarse_ratio = [r['draws'][-1] / r['draws'][0] for r in multi if r['draws'][0]]
        t1 = [r['thr'][0] for r in multi]
        print(f'{name} {cls[8:-1]}: bodies={len(rs)} lod_count={dict(sorted(nl.items()))} '
              f'single_lod={nl[1]} ({100 * nl[1] / max(1, len(rs)):.1f}%) lod0_draws p50={st.median(d0) if d0 else 0} '
              f'mean={st.mean(d0) if d0 else 0:.1f} | multi-LOD: T1 p50={st.median(t1) if t1 else 0} '
              f'coarsest/lod0 draws p50={st.median(coarse_ratio) if coarse_ratio else 0:.2f}')


def main():
    fl = Assets(FL, catalogues=[p.relative_to(FL).as_posix() for p in catalogue_mounts.mounted_catalogues(FL, 'fl')])
    ap = Assets(AP, catalogues=STOCK_AP_CATALOGUES)
    rf, ra = census(fl), census(ap)
    summarise('FL', rf)
    summarise('AP-stock', ra)
    common = set(rf) & set(ra)
    same_src = sum(1 for s in common if rf[s]['src'] == ra[s]['src'])
    same_ladder = sum(1 for s in common if (rf[s]['n'], rf[s]['thr'], rf[s]['draws']) == (ra[s]['n'], ra[s]['thr'], ra[s]['draws']))
    print(f'common stems={len(common)} same winning catalogue={same_src} identical ladder (count, thresholds, draws)={same_ladder}')
    print(f'FL-only stems={len(set(rf) - set(ra))} AP-only stems={len(set(ra) - set(rf))}')
    src = collections.Counter(r['src'] for r in rf.values())
    print('FL winning catalogue per stem:', dict(src.most_common()))
    print('AP winning catalogue per stem:', dict(collections.Counter(r['src'] for r in ra.values()).most_common()))
    diff = [s for s in common if (rf[s]['n'], rf[s]['thr']) != (ra[s]['n'], ra[s]['thr'])]
    print(f'common stems with different LOD count or thresholds={len(diff)}; examples:')
    for s in sorted(diff)[:8]:
        print(f'  {s}: FL n={rf[s]["n"]} thr={rf[s]["thr"]} draws={rf[s]["draws"]} | AP n={ra[s]["n"]} thr={ra[s]["thr"]} draws={ra[s]["draws"]}')
    print('FL-only stems:', ', '.join(sorted(set(rf) - set(ra))[:20]))
    print('\nbodies seen in run-18 frame 15972 (drawn draws, drawn LOD, bbox px) and their ladders')
    from lod_overlay import original_assets
    apm, skipped = original_assets(APM)
    over = Assets(APM)
    for stem, what, n, lod, px in SEEN:
        full = 'objects/' + stem
        line = [f'{what} {stem} drawn={n} lod{lod} bbox={px}']
        for name, a in (('FL', fl), ('AP-stock', ap), ('AP-mayhem', apm), ('AP-installed+overlay', over)):
            try:
                r = ladder(a, full)
                bands = bob1.format_bands(bob1.selection_bands(r['thr'])) if r['thr'] else 'single'
                line.append(f'   {name}: {r["src"]} n={r["n"]} thr={r["thr"]} draws/LOD={r["draws"]} very-high bands: {bands}')
            except Exception as ex:
                line.append(f'   {name}: {type(ex).__name__} {str(ex)[:80]}')
        print('\n'.join(line))
    print(f'(AP-mayhem = installed AP minus overlay catalogues {skipped})')


if __name__ == '__main__':
    main()
