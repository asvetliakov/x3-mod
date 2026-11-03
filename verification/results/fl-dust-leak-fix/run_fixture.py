#!/usr/bin/env python3
"""Builds verification/probe/fl_dust_leak_fix.cpp exactly as verification/analysis/test_fl_dust_leak_fix.py does
(MinGW i686, -mstackrealign -mincoming-stack-boundary=2 -msse2 -mfpmath=sse) and runs it natively through WSL interop
(no Wine), full run and retain run. Writes fixture.json beside this script (counts and summary lines only; the raw
output stays local). Exit 0 = both runs PASS.

usage: python3 verification/results/fl-dust-leak-fix/run_fixture.py
"""
import json
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'verification/analysis'))
from test_fl_dust_leak_fix import build  # noqa: E402

SUMMARY = ('WINDOWS', 'FLIPS', 'FAULTS', 'RUNS', 'RECORDER', 'RETAIN', 'RESULT')


def main():
    report = {}
    with tempfile.TemporaryDirectory(prefix='x3-fl-dust-') as tmp:
        start = time.monotonic()
        exe = build(tmp)
        if exe is None:
            print('needs i686-w64-mingw32-g++ and WSL interop')
            return 2
        report['build_s'] = round(time.monotonic() - start, 2)
        for mode, args in (('full', []), ('retain', ['retain'])):
            start = time.monotonic()
            run = subprocess.run([str(exe), *args], text=True, capture_output=True, timeout=300)
            m = re.search(r'RESULT (PASS|FAIL) checks=(\d+) failures=(\d+)', run.stdout)
            report[mode] = {'exit': run.returncode, 'result': m.group(1) if m else None,
                            'checks': int(m.group(2)) if m else 0, 'failures': int(m.group(3)) if m else -1,
                            'elapsed_s': round(time.monotonic() - start, 2),
                            'summary': [l for l in run.stdout.splitlines() if l.split(' ', 1)[0] in SUMMARY],
                            'fail_lines': [l for l in run.stdout.splitlines() if l.startswith('FAIL')][:20]}
    (HERE / 'fixture.json').write_text(json.dumps(report, indent=1) + '\n')
    for mode in ('full', 'retain'):
        r = report[mode]
        print(mode, r['result'], 'checks=%d failures=%d exit=%d' % (r['checks'], r['failures'], r['exit']))
    return 0 if all(report[m]['result'] == 'PASS' for m in ('full', 'retain')) else 1


if __name__ == '__main__':
    sys.exit(main())
