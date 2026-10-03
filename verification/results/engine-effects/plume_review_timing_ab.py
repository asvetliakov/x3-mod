#!/usr/bin/env python3
"""Plume look review fixes (2026-10-03, docs/verification/engine-effects.md "Plume look review fixes"): the stage's
EVENT-fenced GPU cost and the CPU build before and after, one session, the two fixture builds interleaved.

`--only timing,build` of verification/probe/engine_plumes_fixture.cpp: TIMING gpu_ms = median(tail with the stage) -
median(tail without) - the stage's CPU submit, 60 samples each, 30 / 100 nozzles at 1920x1080 and 5120x1440; BUILD the
CPU build alone (QPC, median of 41 x 50 builds) at 30 / 100 / 1,024 records. "before" is the fixture built from the
look port's head (74bcc57b, the run_engine_plumes.py compile line, kept outside the tree); "after" the tree's
verification/probe/build/engine-plumes/engine_plumes_fixture.exe. Each build runs `--rounds` times, interleaved; the
medians of the rounds go to plume_review_timing_ab_out.json beside this script.

  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py \\
      python3 verification/results/engine-effects/plume_review_timing_ab.py <before.exe> <after.exe> [--rounds 3]
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


def run(exe):
    with tempfile.TemporaryDirectory(prefix='x3-plume-timing-') as work:
        command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=b', '--workdir', work, str(exe), r'C:\X3\d3dx9_37.dll',
                   '--only', 'timing,build']
        done = subprocess.run(command, capture_output=True, env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'), timeout=900)
    gpu, build = {}, {}
    for line in done.stdout.decode('utf-8', 'replace').splitlines():
        f = dict(re.findall(r'(\w+)=(\S+)', line))
        if line.startswith('TIMING '):
            gpu[f"{f['width']}x{f['height']}_{f['nozzles']}"] = float(f['gpu_ms'])
        elif line.startswith('BUILD '):
            build[f['records']] = float(f['median_us'])
    return gpu, build


def summarise(runs, index):
    cases = sorted({k for name in runs for r in runs[name] for k in r[index]}, key=lambda k: (len(k), k))
    out = {}
    for case in cases:
        b = [r[index][case] for r in runs['before'] if case in r[index]]
        a = [r[index][case] for r in runs['after'] if case in r[index]]
        out[case] = {'before': b, 'after': a, 'before_median': round(statistics.median(b), 4) if b else None,
                     'after_median': round(statistics.median(a), 4) if a else None}
    return out


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
            runs[name].append(run(exe))
    record = {'bottle': bottle.describe(), 'rounds': args.rounds, 'before': 'fixture at 74bcc57b (plume look port)',
              'after': 'fixture at the review-fix tree', 'gpu_ms': summarise(runs, 0), 'build_us': summarise(runs, 1)}
    text = json.dumps(record, indent=1) + '\n'
    Path(__file__).with_name('plume_review_timing_ab_out.json').write_text(text)
    print(text, end='')


if __name__ == '__main__':
    main()
