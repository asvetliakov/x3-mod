"""Review check (2026-11-04): a bake that changes nothing must not pass sky_textures.failures().

  python3 verification/results/sector-background-textures/sky_metrics_noop_check.py --game /mnt/c/X3FL

For every bluewell and avdustysol nebula part of the game: the untouched source as its own "bake" and a
nearest-neighbour 2x upscale re-encoded with lod_atlas.encode_dxt; prints the verdict per part. Expected: every row
FAIL (the near-uniform branch needs the run cut to a third, the others the plateau limits).
"""
import argparse
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
import lod_atlas  # noqa: E402
import sky_textures as st  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument('--game', type=Path, required=True)
a = ap.parse_args()
assets = st.source_view(a.game.resolve())
passed = 0
for fam in ('bluewell', 'avdustysol'):
    for n in range(1, 5):
        member = f'dds/nebula_{fam}_background_part_{n:02d}_diff.pck'
        src = assets.read_entry(assets.candidates(member)[-1])
        w, h, _, _ = lod_atlas.dds_format(src)
        nn = np.repeat(np.repeat(lod_atlas.decode_dds(src), 2, 0), 2, 1)
        nn_dds = lod_atlas.dds_header(2 * w, 2 * h, 1, 'DXT1') + lod_atlas.encode_dxt(nn, 'DXT1')
        for label, bake in (('untouched', src), ('nearest 2x', nn_dds)):
            bad = st.failures(st.metrics(src, bake))
            passed += not bad
            print(f"{member} {label}: {'PASS' if not bad else 'FAIL ' + '; '.join(bad)}")
print(f'no-op bakes passing: {passed} (expected 0)')
sys.exit(1 if passed else 0)
