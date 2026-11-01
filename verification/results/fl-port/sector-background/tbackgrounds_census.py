#!/usr/bin/env python3
"""FL TBackgrounds census for docs/reverse-engineering/fl/sector-fog.md section 6 (2026-11-01).

Reads every types/TBackgrounds member of /mnt/c/X3FL{,/addon,/addon2} catalogues and prints, per member,
the header version/count and the bounds the shared sector_background reader enforces (dust 0..64,
fog near/far, family name length < 32). Prints values only; no game bytes are written."""
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import sector_fog_census as s  # noqa: E402

game = Path(sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3FL')
cats = sorted(game.glob('[0-9][0-9].cat')) + sorted((game / 'addon').glob('[0-9][0-9].cat')) + \
    sorted((game / 'addon2').glob('[0-9][0-9].cat'))
for cat in cats:
    for e in s.read_catalogue(cat):
        if 'tbackgrounds' not in e['path'].lower():
            continue
        with cat.with_suffix('.dat').open('rb') as stream:
            stream.seek(e['offset'])
            data = s.unpack(bytes(v ^ 0x33 for v in stream.read(e['size'])))
        rows = s.backgrounds(data)
        fog = [r for r in rows if r['effective_far_native']]
        print(f"{cat.relative_to(game)} {e['path']} records={len(rows)} dust_max={max(r['dust'] for r in rows)} "
              f"dust_rows={sum(1 for r in rows if r['dust'] > 0)} fog_rows={len(fog)} "
              f"far_max={max(r['far_native'] for r in rows)} near_gt_far={sum(1 for r in rows if r['far_native'] < r['near_native'])} "
              f"name_max={max(len(r['family']) for r in rows)} families={len({r['family'] for r in rows})}")
