#!/usr/bin/env python3
"""Per-body bake seconds of two scratch batch records (x3m-lod-batch.json under each --out): the sum (worker
seconds), the ratio new/old per body (contention), and the slowest body's share of the bake phase (the tail).

  python3 ab_seconds.py OLD_OUT NEW_OUT
"""
import json
import sys
from pathlib import Path

recs = [json.load(open(Path(d) / 'x3m-lod-batch.json')) for d in sys.argv[1:3]]
secs = [{b['name']: b['seconds'] for b in r['bodies'] if b.get('seconds') is not None} for r in recs]
common = sorted(set(secs[0]) & set(secs[1]))
for label, r, s in zip(('old', 'new'), recs, secs):
    top = max(s.values())
    print(f"{label}: {len(s)} bodies, sum {sum(s.values()):.0f} worker-s, bake {r['timing']['bake_s']} s with"
          f" {r['jobs']} workers -> {sum(s.values()) / r['timing']['bake_s']:.1f} busy workers on average;"
          f" slowest body {top:.1f} s")
ratios = sorted(secs[1][n] / secs[0][n] for n in common if secs[0][n] > 0)
print(f'per-body seconds new/old over {len(ratios)} bodies: median {ratios[len(ratios) // 2]:.2f},'
      f' p90 {ratios[int(.9 * len(ratios))]:.2f}')
