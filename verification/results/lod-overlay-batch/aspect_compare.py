#!/usr/bin/env python3
"""Aspect-aware switch threshold (2026-09-23): the census over the bodies of eligible_bodies.txt with
the aspect factor on (default K_max 1.5 ships / 2.0 stations) and off (--no-aspect), at the batch
defaults (reference width 1800 for 1920x1080, atlas sizes 1024/2048, --min-texels 0.5,
--texel-floor-share 0.10). Reports the bodies whose T changes, the census refusals, the batch
texel_floor refusals and the t_pad_below_t1 column (a waived guard for source record 0, not a
refusal) in both runs, and per body k, T_class -> T, ratios and, where a flown radius is known
(--radius-log, run272 burst census), the switch distance in km (~505 units/m, inferred).
Read-only; nothing is baked.

  python3 verification/results/lod-overlay-batch/aspect_compare.py [--jobs N] [--radius-log FILE] > aspect_compare_out.txt
"""
import argparse
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / 'tools' / 'analysis'))
import bob1                        # noqa: E402
import lod_atlas                   # noqa: E402
import lod_batch_census as census  # noqa: E402

WIDTH, SIZES, MIN_TEXELS, SHARE = 1800, (1024, 2048), 0.5, 0.10


def texel(r):
    est = (r.get('atlas') or {}).get(WIDTH)
    if not est:
        return None, None, False
    x = lod_atlas.texel_floor(est['tiles'], MIN_TEXELS, SHARE)
    return est['ratio'], x['weighted_texels_per_px'], r['eligible'] and x['refuse']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--radius-log', action='append', type=Path)
    a = ap.parse_args()
    lines = [line.split('#', 1)[0] for line in (Path(__file__).with_name('eligible_bodies.txt')).read_text().splitlines()]
    names = [line.split('=', 1)[0].strip() for line in lines if '=' in line]
    only = {census.body_key(n) for n in names}
    base = dict(sizes=SIZES, include_other=False, widths=(WIDTH,))
    runs = {}
    radii = census.world_radii(a.radius_log or census.default_radius_logs())
    for label, rule in (('on', dict(census.RULE)), ('off', dict(census.RULE, aspect=False))):
        rows, _ = census.run(bob1.DEFAULT_GAME, dict(base, rule=rule), a.jobs, only=only, include_text=True)
        census.attach_world(rows, radii)
        runs[label] = {r['name']: r for r in rows}
    on, off = runs['on'], runs['off']
    common = sorted(set(on) & set(off))
    changed = [n for n in common if on[n].get('t_pad') != off[n].get('t_pad')]
    refused = lambda rs, n: rs[n]['refuse'] or not rs[n]['eligible']
    print(f'bodies {len(names)} (eligible_bodies.txt), censused {len(common)}; width {WIDTH}, sizes {list(SIZES)},'
          f' --min-texels {MIN_TEXELS}, --texel-floor-share {SHARE}; flown radii {len(radii)} bodies')
    print(f'T changed by the aspect factor: {len(changed)} (ships {sum(1 for n in changed if on[n]["cat"] == "ship")},'
          f' stations/others {sum(1 for n in changed if on[n]["cat"] != "ship")});'
          f' at the cap: {sum(1 for n in changed if on[n]["t_pad"] == round(on[n]["t_class"] * (1.5 if on[n]["cat"] == "ship" else 2.0)))}')
    for label, rs in (('off', off), ('on', on)):
        tf = [n for n in common if texel(rs[n])[2]]
        print(f'aspect {label}: census refusals/filters {sum(1 for n in common if refused(rs, n))},'
              f' texel_floor {len(tf)} {tf}, t_pad_below_t1 {sum(1 for n in common if rs[n].get("t_pad_below_t1"))},'
              f' ratio < 2 {sum(1 for n in common if (texel(rs[n])[0] or 9) < 2)}')
    newly = [n for n in common if (refused(on, n) or texel(on[n])[2]) and not (refused(off, n) or texel(off[n])[2])]
    print(f'newly refused with the aspect factor: {len(newly)} {newly}')
    print(f'newly t_pad_below_t1: {[n for n in common if on[n].get("t_pad_below_t1") and not off[n].get("t_pad_below_t1")]}')
    nbytes = lambda rs, n: ((rs[n].get('atlas') or {}).get(WIDTH) or {}).get('bytes', 0)
    print(f'atlas bytes (census estimate, eligible rows) off {sum(nbytes(off, n) for n in common if off[n]["eligible"])}'
          f' -> on {sum(nbytes(on, n) for n in common if on[n]["eligible"])}; atlas size changes'
          f' {sum(1 for n in common if (off[n].get("atlas") or {}).get(WIDTH, {}).get("size") != (on[n].get("atlas") or {}).get(WIDTH, {}).get("size"))}')
    below2 = [n for n in changed if (texel(on[n])[0] or 9) < 2 <= (texel(off[n])[0] or 9)]
    print(f'ratio falls below 2.0 with the aspect factor: {len(below2)} {below2}')
    print('per body with a changed T: name k T_class->T min_ratio off->on weighted on, atlas size/bytes off->on,'
          ' switch km (flown radius)')
    for n in changed:
        r, ro = on[n], off[n]
        (m1, w1, _), (m0, _, _) = texel(r), texel(ro)
        f = lambda x: '-' if x is None else f'{x:.3f}'
        a0, a1 = (ro.get('atlas') or {}).get(WIDTH) or {}, (r.get('atlas') or {}).get(WIDTH) or {}
        print(f'  {n} k={r["aspect_k"]:.3f} T {r["t_class"]}->{r["t_pad"]} min {f(m0)}->{f(m1)} weighted {f(w1)}'
              f' atlas {a0.get("size")}/{a0.get("bytes")}->{a1.get("size")}/{a1.get("bytes")}'
              + (f' D {r["switch_km_class"]:.2f}->{r["switch_km"]:.2f} km' if r.get('switch_km') else '')
              + (f' ({r["aspect_note"]})' if r.get('aspect_note') else ''))


if __name__ == '__main__':
    main()
