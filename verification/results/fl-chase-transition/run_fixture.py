#!/usr/bin/env python3
"""Builds and runs the FL chase view restore native fixture (verification/probe/fl_chase_transition.cpp) as
verification/analysis/test_fl_chase_transition.py does, and writes fixture_out.txt beside this script: every summary,
SCENARIO and FAIL line plus the install rows of the first (successful) install. Needs MinGW i686 and WSL interop (a
native Windows run); no game, no Wine. Also writes verify_fl_sites.json (verify_chase_transition_sites_fl.py --json).

usage: python3 -I verification/results/fl-chase-transition/run_fixture.py
"""
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'verification/analysis'))
sys.path.insert(0, str(ROOT / 'verification/probe'))
import test_fl_chase_transition as t  # noqa: E402
import verify_chase_transition_sites_fl as v  # noqa: E402

with tempfile.TemporaryDirectory(prefix='x3-fl-ct-') as tmp:
    tmp = Path(tmp)
    memory = tmp / 'engine_memory.o'
    subprocess.run([t.compiler(), *t.COMMON, '-mno-sse', '-mno-mmx', '-mfpmath=387', '-c',
                    str(ROOT / 'src/proxy/engine_memory.cpp'), '-o', str(memory)], check=True)
    exe = tmp / 'fl_chase_transition.exe'
    subprocess.run([t.compiler(), *t.COMMON, '-fno-exceptions', '-static', str(t.FIXTURE), str(memory), '-o', str(exe)],
                   check=True)
    start = time.perf_counter()
    run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=300)
    seconds = time.perf_counter() - start
lines = run.stdout.splitlines()
first_install = []
for line in lines:
    if line.startswith('LOG chase_'):
        first_install.append(line)
    elif first_install:
        break
keep = first_install + [l for l in lines if not l.startswith('LOG ')]
(HERE / 'fixture_out.txt').write_text('\n'.join(keep) + f'\n# exit={run.returncode} run_seconds={seconds:.2f} '
                                      f'lines={len(lines)}\n')
if v.FL_DEFAULT_EXE.is_file():
    report = v.verify(v.FL_DEFAULT_EXE.read_bytes())
    report['exe'] = str(v.FL_DEFAULT_EXE)
    for s in report['checks']['sites']['all']:
        s.pop('text', None)
    (HERE / 'verify_fl_sites.json').write_text(json.dumps(report, indent=1) + '\n')
    print('verifier', report['result'])
print(lines[-1] if lines else run.stderr[-500:])
sys.exit(run.returncode)
