#!/usr/bin/env python3
"""Engine plumes, phase 2 review fix 7 (docs/verification/engine-effects.md): the stage's EVENT-fenced GPU cost before
and after the overdraw reduction in one session, alternating the two builds of verification/probe/engine_plumes_fixture.cpp
(`--only timing`: 30 / 100 nozzles at 1920x1080 and 5120x1440, gpu_ms = median(tail with the stage) - median(tail
without) - the stage's CPU submit, 60 samples each). The "before" executable is the fixture built from the phase-2 head
(f8568972: git archive of src/ and the fixture, the run_engine_plumes.py compile line); "after" is the tree's
verification/probe/build/engine-plumes/engine_plumes_fixture.exe. Runs each build `--rounds` times, interleaved.

  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py \
      python3 verification/results/engine-effects/phase2_stage_timing_ab.py <before.exe> <after.exe> [--rounds 3]
"""
import argparse
import json
import os
import re
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import bottle  # noqa: E402
from game_guard import game_running  # noqa: E402


def timing(exe):
    with tempfile.TemporaryDirectory(prefix='x3-plume-timing-') as work:
        command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=b', '--workdir', work, str(exe), r'C:\X3\d3dx9_37.dll',
                   '--only', 'timing']
        done = subprocess.run(command, capture_output=True, env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'), timeout=600)
    text = done.stdout.decode('utf-8', 'replace')
    rows = {}
    for line in text.splitlines():
        if line.startswith('TIMING '):
            f = dict(re.findall(r'(\w+)=(\S+)', line))
            rows[f"{f['width']}x{f['height']}_{f['nozzles']}"] = float(f['gpu_ms'])
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('before', type=Path)
    parser.add_argument('after', type=Path)
    parser.add_argument('--rounds', type=int, default=3)
    args = parser.parse_args()
    if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3':
        raise SystemExit('requires X3M_FIXTURE_BOTTLE=X3')
    if game_running():
        raise SystemExit('the game is running')
    runs = {'before': [], 'after': []}
    for _ in range(args.rounds):
        for name, exe in (('before', args.before), ('after', args.after)):
            runs[name].append(timing(exe))
    cases = sorted({k for r in runs['before'] + runs['after'] for k in r})
    summary = {}
    for case in cases:
        b = [r[case] for r in runs['before'] if case in r]
        a = [r[case] for r in runs['after'] if case in r]
        summary[case] = {'before_ms': b, 'after_ms': a, 'before_median': round(statistics.median(b), 4) if b else None,
                         'after_median': round(statistics.median(a), 4) if a else None}
    print(json.dumps({'bottle': bottle.describe(), 'rounds': args.rounds, 'gpu_ms': summary}, indent=1))


if __name__ == '__main__':
    main()
