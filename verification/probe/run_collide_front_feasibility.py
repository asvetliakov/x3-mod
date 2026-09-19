#!/usr/bin/env python3
"""Build (host only) and run the front-tracking feasibility measurement in the X3 bottle; write a compact record.

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_collide_front_feasibility.py
A measurement, not a regression fixture: no production code and no engine bytes are involved
(docs/reverse-engineering/sector-collide.md, section 14.9).
"""
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path
import bottle
from run_chase_aim_trace import FLAGS
ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/verification/collide-front-feasibility'
EXE = BUILD / 'collide_front_feasibility.exe'
OUT = ROOT / 'verification/results/collide-front-feasibility.json'


def _fields(text):
    return {k: (v if not re.fullmatch(r'-?[\d.]+', v) else float(v) if '.' in v else int(v)) for k, v in re.findall(r'(\w+)=(\S+)', text)}


def main():
    name = os.environ.get('X3M_FIXTURE_BOTTLE')
    if name != 'X3':
        sys.exit('set X3M_FIXTURE_BOTTLE=X3')
    BUILD.mkdir(parents=True, exist_ok=True)
    subprocess.run(['i686-w64-mingw32-g++', *FLAGS, '-I', str(ROOT / 'src/proxy'), str(ROOT / 'verification/probe/collide_front_feasibility.cpp'), '-static', '-static-libgcc',
                    '-static-libstdc++', '-Wl,--large-address-aware', '-o', str(EXE)], check=True, cwd=ROOT)
    started = time.time()
    run = subprocess.run([bottle.WINE, *bottle.wine_args(name), str(EXE)], capture_output=True, text=True, timeout=3000)
    lines = run.stdout.splitlines()
    record = {'exit_status': run.returncode, 'elapsed_s': round(time.time() - started, 1), 'scenes': [_fields(l[6:]) for l in lines if l.startswith('SCENE ')],
              'runs': [_fields(l[4:]) for l in lines if l.startswith('RUN ')], 'failure_lines': [l for l in lines if l.startswith('FAIL')], 'bottle': bottle.describe(name),
              'note': 'medians over up to 200 frames; full = the replica descent of section 13 (engine parity per node pair); diagnostic timings, not game FPS'}
    OUT.write_text(json.dumps(record, indent=1) + '\n')
    print(json.dumps({k: record[k] for k in ('exit_status', 'elapsed_s', 'scenes', 'runs', 'failure_lines')}))
    if run.returncode != 0:
        print(run.stdout[-1500:], run.stderr[-1500:], file=sys.stderr)
    sys.exit(run.returncode)


if __name__ == '__main__':
    main()
