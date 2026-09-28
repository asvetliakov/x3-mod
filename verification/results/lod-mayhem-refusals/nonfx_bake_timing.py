#!/usr/bin/env python3
"""Classic (non-effect) material bake, 2026-09-29: cost of the planning view (lod_atlas.classic_view) per body.

  python3 nonfx_bake_timing.py [--game GAME] > nonfx_bake_timing_out.txt

Over the ship/station bodies the installed record refuses non_effect_material (binary members): wall time of
classic_view against the time to read and parse the body, and the number of texture decodes it causes (none
expected: the view resolves names, it decodes nothing). Read-only.
"""
import argparse
import json
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / 'tools/analysis'), str(ROOT / 'verification/probe')]
import bob1                                          # noqa: E402
import lod_atlas                                     # noqa: E402
import lod_overlay                                   # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', type=Path, default=bob1.DEFAULT_GAME)
    a = ap.parse_args()
    rec = json.loads((a.game / 'addon' / 'x3m-lod-batch.json').read_text())
    assets = lod_overlay.original_assets(a.game)[0]
    by = {f'{e["source"]}:{e["path"]}'.lower(): e for lst in assets.entries.values() for e in lst}
    decodes = []
    real = lod_atlas.decode_dds
    lod_atlas.decode_dds = lambda *x, **k: decodes.append(1) or real(*x, **k)
    rows = []
    for b in rec['bodies']:
        e = by.get(b['member'].lower())
        if 'non_effect_material' not in (b.get('refuse') or []) or b['cat'] == 'other' or e is None \
                or not e['path'].lower().endswith(('.pbb', '.bob')):
            continue
        t0 = time.perf_counter()
        mats = bob1.materials(bob1.parse(assets.read_entry(e), lod_overlay.MAX_TRAILING))
        t1 = time.perf_counter()
        view = lod_atlas.classic_view(assets, mats)
        t2 = time.perf_counter()
        assets.cache.clear()
        rows.append((t2 - t1, t1 - t0, sum(1 for m in view if 'classic' in m or 'classic_keep' in m), b['name']))
    view_s, parse_s = sum(r[0] for r in rows), sum(r[1] for r in rows)
    print(f'bodies {len(rows)}; classic records {sum(r[2] for r in rows)}; classic_view {1000 * view_s:.1f} ms in total'
          f' ({1000 * view_s / len(rows):.2f} ms per body, {100 * view_s / parse_s:.1f} % of read + parse'
          f' {1000 * parse_s:.0f} ms); texture decodes {len(decodes)}')
    for v, p, n, name in sorted(rows, reverse=True)[:3]:
        print(f'  {1000 * v:.2f} ms  classic records {n}  read + parse {1000 * p:.0f} ms  {name}')


main()
