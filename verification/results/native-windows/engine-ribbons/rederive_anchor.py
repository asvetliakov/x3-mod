"""Re-derivation of the ribbons fixture for the anchored head (2026-11-05): per resolve row, SETA and cut, the anchored
law's numbers from the native record next to the Wine record's nozzle-anchored ones. Prints only those rows.
Usage, from the checkout: python3 verification/results/native-windows/engine-ribbons/rederive_anchor.py"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
wine = json.loads((ROOT / 'verification/results/bottle-X3/engine-ribbons/summary.json').read_text())['report']
native = json.loads((Path(__file__).parent / 'summary.json').read_text())
r = native['report']
print('native passed', native['passed'], 'checks', r['check_count'], 'failed', len(r['failed_checks']), 'commit', native['source']['commit'][:12])
i0 = 0.9  # engine_ribbons_core.h radiance_high
for w, n in zip(wine['ribbon_resolve'], r['ribbon_resolve']):
    strip = n['visible_px'] - n['anchor_px'] + n['expected_px']
    print(f"resolve {n['width']} {n['sky']} {n['speed_px']:.0f}px: A {n['anchor_px']} L_vis {n['visible_px']} strip {strip:.1f} | "
          f"at_nozzle {w['at_nozzle']} -> {n['at_nozzle']} (<= {.02 * i0:.3f}), at_visible_end {n['at_visible_end']} (>= {.5 * i0:.2f}), "
          f"gaps {w['gaps']} -> {n['gaps']} | trail behind the plume {n['length_px'] - n['visible_px']:.1f} (Wine length {w['length_px']}, "
          f"L {n['expected_px']}, |d| <= {.1 * n['expected_px']:.0f}) | survival {w['survival']} -> {n['survival']} (>= 0.9)")
for w, n in zip(wine['ribbon_seta'], r['ribbon_seta']):
    print('seta', {k: (w.get(k), n.get(k)) for k in ('anchor_px', 'gaps', 'weakest', 'jumps', 'live')})
for w, n in zip(wine['ribbon_cut'], r['ribbon_cut']):
    print('cut', {k: (w.get(k), n.get(k)) for k in ('old_px', 'new_px', 'bridge_without_cut', 'bridge_with_cut', 'cut_clears')})
