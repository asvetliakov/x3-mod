#!/usr/bin/env python3
"""Builds and runs the FL scene-end hook native fixture (verification/probe/fl_scene_hook.cpp) through the host
test's build_and_run (verification/analysis/test_fl_scene_hook.py) and writes fixture.json beside this script:
the RESULT, TIMING and ANCHOR lines, the fl.scene rows and the signal-path CPU-state audit. WSL2 with interop only."""
import importlib.util
import json
import re
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
spec = importlib.util.spec_from_file_location('test_fl_scene_hook', ROOT / 'verification/analysis/test_fl_scene_hook.py')
test = importlib.util.module_from_spec(spec)
spec.loader.exec_module(test)


def main():
    with tempfile.TemporaryDirectory(prefix='x3-fl-scene-') as tmp:
        outcome = test.build_and_run(tmp)
    if outcome is None:
        print('needs i686-w64-mingw32-g++, clang and WSL interop')
        return 2
    run, audit = outcome
    lines = run.stdout.splitlines()
    record = {'exit_code': run.returncode, 'result': next((l for l in lines if l.startswith('RESULT ')), None),
              'timing': next((l for l in lines if l.startswith('TIMING ')), None),
              'anchor': next((l for l in lines if l.startswith('ANCHOR ')), None),
              'failures': [l for l in lines if l.startswith('FAIL ')],
              'rows': [re.sub(r'^LOG ', '', l) for l in lines if l.startswith('LOG fl.scene')],
              'audit_exit': audit.returncode, 'audit': json.loads(audit.stdout) if audit.stdout.strip() else None}
    (HERE / 'fixture.json').write_text(json.dumps(record, indent=1) + '\n')
    print(json.dumps({k: record[k] for k in ('exit_code', 'result', 'timing', 'anchor', 'audit_exit')}))
    return 0 if run.returncode == 0 and audit.returncode == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
