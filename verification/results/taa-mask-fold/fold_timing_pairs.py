#!/usr/bin/env python3
"""FOLD_TIMING of two temporal fixture builds, alternated (docs/verification/temporal-resolve.md, "Mask fold"): the pre-fold
baseline (the committed tree's temporal_pass_fixture.cpp with verification/probe/temporal_fold_timing_inc.h included and its
fold-timing mode, built with -DX3M_FOLD_BASELINE) and the fold build, each run in its own `fold-timing` mode ROUNDS times,
ABAB. Prints the rows prefixed by the round. Run under the Wine lock:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/results/taa-mask-fold/fold_timing_pairs.py \\
      <baseline-root> <fold-root> [rounds]
where each root holds verification/probe/build/temporal_pass_fixture.exe and src/temporal/*.hlsl of its tree."""
import os
import subprocess
import sys
from pathlib import Path

WINE = '/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine'
roots = {'baseline': Path(sys.argv[1]), 'fold': Path(sys.argv[2])}
rounds = int(sys.argv[3]) if len(sys.argv) > 3 else 3
for n in range(rounds):
    for build, root in roots.items():
        exe = root / 'verification/probe/build/temporal_pass_fixture.exe'
        command = [WINE, '--bottle', 'X3', '--no-update', '--dll', 'd3d9=b', '--workdir', str(exe.parent), str(exe), r'C:\X3\d3dx9_37.dll',
                   'Z:' + str(root / 'src/temporal/depth_decode.hlsl'), 'Z:' + str(root / 'src/temporal/resolve.hlsl'),
                   'Z:' + str(root / 'src/temporal/taa_sharpen_ps.hlsl'), 'fold-timing']
        run = subprocess.run(command, capture_output=True, text=True, env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'), timeout=900)
        rows = [line for line in run.stdout.splitlines() if line.startswith('FOLD_TIMING build=')]
        assert run.returncode == 0 and rows, (build, run.returncode, run.stdout[-500:])
        for row in rows:
            print(f'round={n} {row}')
        sys.stdout.flush()
