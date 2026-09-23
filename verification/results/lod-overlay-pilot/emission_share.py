#!/usr/bin/env python3
"""Rule f (body_materials.py) in emission terms: per body and P, the share of the
candidates' light-map emission (face area x mean light-map luma, both from the census)
that the kept set covers, next to its area share; the glow set's emission is listed
separately (always kept). Prints numbers only; reads the shipped catalogues.

  python3 verification/results/lod-overlay-pilot/emission_share.py <body> ...
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import bob1  # noqa: E402
import body_materials  # noqa: E402

assets, skipped = body_materials.original_assets(bob1.DEFAULT_GAME)
print(f'sources read without {skipped}')
for name in sys.argv[1:]:
    c = body_materials.census(bob1.parse(assets.read_entry(bob1.resolve_body(assets, name))), None, assets)
    rows = c['materials']
    emit = {mi: rows[mi]['area'] * (rows[mi]['light_info'] or {}).get('luma', 0.0) for mi in rows}
    cand = c['real_light'] - c['glow']
    total = sum(emit[mi] for mi in cand)
    glow = sum(emit[mi] for mi in c['glow'])
    print(f'{name}: candidate emission {total:.4g} (area-luma units), glow set emission {glow:.4g}'
          f' ({glow / (glow + total) if glow + total else 0:.3f} of all real-light-map emission)')
    for p, a in c['area'].items():
        kept = sum(emit[mi] for mi in a['kept'])
        print(f'  f{p:g}: draws={c["rules"][f"f{p:g}"]} area covered {a["covered"] / a["total"]:.3f}'
              f' emission covered {kept / total if total else 0:.3f}')
    best = sorted(cand, key=lambda m: -emit[m])[:5]
    print('  top emitters (mat share_of_candidate_emission area_share_of_record p99): '
          + '; '.join(f'{mi} {emit[mi] / total:.3f} {rows[mi]["share"]:.3f}'
                      f' {(rows[mi]["light_info"] or {}).get("p99", 0):.2f}' for mi in best))
