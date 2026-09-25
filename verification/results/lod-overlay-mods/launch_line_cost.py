#!/usr/bin/env python3
"""Cost and text of the implemented `lod overlay:` launch line (tools/analysis/lod_overlay_check.py) on the
real bottle X3, game closed. Read-only: markers, stats, cat hashes, user.reg; no dat is opened (counted).
    python3 verification/results/lod-overlay-mods/launch_line_cost.py > verification/results/lod-overlay-mods/launch_line_cost_out.txt"""
import builtins
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import lod_overlay_check as check  # noqa: E402

game = check.DEFAULT_GAME
opened, real_open, real_path_open = [], builtins.open, Path.open
builtins.open = lambda f, *a, **k: (opened.append(str(f)), real_open(f, *a, **k))[1]
Path.open = lambda self, *a, **k: (opened.append(str(self)), real_path_open(self, *a, **k))[1]
times = []
for _ in range(3):
    opened.clear()
    t = time.perf_counter()
    line = check.overlay_line(game)
    times.append((time.perf_counter() - t) * 1000)
builtins.open, Path.open = real_open, real_path_open
print(line)
print(f'overlay_line: {", ".join(f"{t:.1f}" for t in times)} ms over 3 runs; files opened per run {len(opened)}'
      f' (.dat {sum(1 for p in opened if p.lower().endswith(".dat"))}, .cat {sum(1 for p in opened if p.lower().endswith(".cat"))},'
      f' markers {sum(1 for p in opened if p.endswith(".x3m-lod.json"))}, user.reg {sum(1 for p in opened if p.endswith("user.reg"))})')
