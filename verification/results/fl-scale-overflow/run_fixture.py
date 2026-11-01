#!/usr/bin/env python3
"""Builds and runs the FL scaler-replacement native fixture (verification/probe/fl_scale_overflow.cpp) through the
host test's build_and_run (verification/analysis/test_fl_scale_overflow.py) and writes fixture.json beside this
script: the RESULT, GATE, ORIGINAL_DUMP, REPLACEMENT_DUMP, SWEEP and TIMING lines, the fl.scale_fix rows and the
per-call CPU-state audit. WSL2 with interop and the local X3FL.exe only (its bytes stay in a temporary file)."""
import importlib.util
import json
import re
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
spec = importlib.util.spec_from_file_location('test_fl_scale_overflow', ROOT / 'verification/analysis/test_fl_scale_overflow.py')
test = importlib.util.module_from_spec(spec)
spec.loader.exec_module(test)
LINES = ('RESULT', 'GATE', 'ORIGINAL_DUMP', 'REPLACEMENT_DUMP', 'SWEEP', 'TIMING')


def main():
    with tempfile.TemporaryDirectory(prefix='x3-fl-scale-') as tmp:
        outcome = test.build_and_run(tmp)
    if outcome is None:
        print('needs i686-w64-mingw32-g++, WSL interop and X3FL.exe')
        return 2
    run, audit = outcome
    lines = run.stdout.splitlines()
    record = {'exit_code': run.returncode,
              **{k.lower(): next((l for l in lines if l.startswith(k + ' ')), None) for k in LINES},
              'failures': [l for l in lines if l.startswith('FAIL ')],
              'rows': [re.sub(r'^LOG ', '', l) for l in lines if l.startswith('LOG fl.scale_fix')],
              'audit_exit': audit.returncode, 'audit': json.loads(audit.stdout) if audit.stdout.strip() else None}
    (HERE / 'fixture.json').write_text(json.dumps(record, indent=1) + '\n')
    print(json.dumps({k: record[k] for k in ('exit_code', *(k.lower() for k in LINES), 'audit_exit')}))
    return 0 if run.returncode == 0 and audit.returncode == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
