#!/usr/bin/env python3
"""CPU statistic cost only; run Windows mode through wine_lock.py. --host avoids Wine."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys

from bottle import BOTTLE, WINE, describe, results_dir, wine_args
from game_guard import game_running

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ('src/renderer/exposure.h', 'src/renderer/exposure.cpp',
           'verification/probe/exposure_statistics_benchmark.cpp',
           'verification/probe/run_exposure_statistics.py', 'verification/probe/bottle.py',
           'verification/probe/game_guard.py', 'verification/probe/wine_lock.py')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', action='store_true')
    args = parser.parse_args()
    prefix = 'exposure-statistics-host' if args.host else 'exposure-statistics'
    out = ROOT / 'verification/results' if args.host else results_dir(ROOT)
    out.mkdir(parents=True, exist_ok=True)
    summary = out / (prefix + '.json')
    report = {'passed': False, 'scope': 'CPU statistic diagnostic, no GPU/frame/FPS measurement'}
    summary.write_text(json.dumps(report, indent=2) + '\n')
    try:
        report['sources_before'] = {name: digest(ROOT / name) for name in SOURCES}
        compiler = shutil.which('c++' if args.host else 'i686-w64-mingw32-g++')
        assert compiler, 'compiler unavailable'
        flags = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror']
        if not args.host:
            flags += ['-msse2', '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2',
                      '-static', '-static-libgcc', '-static-libstdc++']
        # Keep each bottle's executable available for the final hash audit;
        # MinGW PE timestamps can differ across otherwise identical rebuilds.
        build = ROOT / 'verification/probe/build/exposure-statistics' / ('native-host' if args.host else 'bottle-' + BOTTLE)
        build.mkdir(parents=True, exist_ok=True)
        exe = build / ('host' if args.host else 'exposure_statistics.exe')
        command = [compiler, *flags, str(ROOT / SOURCES[1]), str(ROOT / SOURCES[2]), '-o', str(exe)]
        report.update(compiler=compiler, compiler_sha256=digest(compiler), command=command,
                      compiler_version=subprocess.check_output([compiler, '--version'], text=True).splitlines()[0])
        subprocess.run(command, check=True, capture_output=True, text=True, timeout=180)
        assert report['sources_before'] == {name: digest(ROOT / name) for name in SOURCES}, 'source changed during build'
        report['executable'] = {'path': str(exe), 'sha256': digest(exe)}
        report['platform'] = platform.platform()
        if args.host:
            run = [str(exe)]
        else:
            report['bottle_before'] = describe()
            assert not game_running(), 'game active'
            run = [WINE, *wine_args(), str(exe)]
        report['run_command'] = run
        env = os.environ.copy()
        env['WINEDEBUG'] = '-all'
        completed = subprocess.run(run, capture_output=True, text=True, env=env, timeout=180)
        raw = out / (prefix + '.txt')
        raw.write_text(completed.stdout)
        report['stdout'] = {'path': str(raw), 'sha256': digest(raw)}
        report['stderr'] = completed.stderr
        assert completed.returncode == 0, f'exit {completed.returncode}'
        lines = completed.stdout.splitlines()
        assert len(lines) == 7 and lines[-1] == 'RESULT PASS cases=6', 'wrong/duplicate terminal inventory'
        cases = []
        expected = [(w, h, dense) for w, h in ((80, 48), (80, 23), (128, 128)) for dense in (0, 1)]
        pattern = r'CASE width=(\d+) height=(\d+) dense=([01]) tiles=(\d+) lit=(\d+) batches=41 repetitions=40 warmup=20 median_us=([\d.]+) p95_us=([\d.]+) min_us=([\d.]+) witness=(-?[\d.]+)'
        for line, identity in zip(lines[:-1], expected):
            match = re.fullmatch(pattern, line)
            assert match, 'malformed case'
            w, h, dense, tiles, lit = map(int, match.groups()[:5])
            median, p95, minimum, witness = map(float, match.groups()[5:])
            assert (w, h, dense) == identity and tiles == w * h
            assert lit == (tiles if dense else (tiles + 9) // 10)
            assert all(math.isfinite(v) for v in (median, p95, minimum, witness)) and 0 < minimum <= median <= p95
            cases.append(dict(width=w, height=h, dense=bool(dense), tiles=tiles, lit=lit,
                              median_us=median, p95_us=p95, min_us=minimum))
        report['cases'] = cases
        report['batches_per_case'] = 41
        report['calls_per_batch'] = 40
        report['warmup_calls'] = 20
        report['sources_after'] = {name: digest(ROOT / name) for name in SOURCES}
        assert report['sources_before'] == report['sources_after'], 'source changed during run'
        assert report['executable']['sha256'] == digest(exe) and report['compiler_sha256'] == digest(compiler)
        if not args.host:
            report['bottle_after'] = describe()
            assert report['bottle_before'] == report['bottle_after'], 'bottle configuration changed'
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
    summary.write_text(json.dumps(report, indent=2) + '\n')
    print(f"{'PASS' if report['passed'] else 'FAIL'} {summary}")
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
