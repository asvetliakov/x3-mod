#!/usr/bin/env python3
"""Builds and runs the FL fog prefill native fixture (verification/probe/fl_fog_prefill.cpp) through
the host test's build_and_run (verification/analysis/test_fl_fog_prefill.py) and writes fixture.json beside
this script: the RESULT, ANCHOR, FOUND and TIMING lines, failures and the fl.prefill rows. WSL2 with interop only."""
import importlib.util
import json
import re
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
spec = importlib.util.spec_from_file_location('test_fl_fog_prefill',
                                              ROOT / 'verification/analysis/test_fl_fog_prefill.py')
test = importlib.util.module_from_spec(spec)
spec.loader.exec_module(test)


def main():
    with tempfile.TemporaryDirectory(prefix='x3-fl-prefill-') as tmp:
        run = test.build_and_run(tmp)
    if run is None:
        print('needs i686-w64-mingw32-g++ and WSL interop')
        return 2
    lines = run.stdout.splitlines()
    first = lambda tag: next((l for l in lines if l.startswith(tag + ' ')), None)
    record = {'exit_code': run.returncode, 'result': first('RESULT'), 'anchor': first('ANCHOR'), 'found': first('FOUND'),
              'timing': first('TIMING'), 'failures': [l for l in lines if l.startswith(('FAIL ', 'STATUS '))],
              'rows': [re.sub(r'^LOG ', '', l) for l in lines if l.startswith('LOG fl.prefill')]}
    (HERE / 'fixture.json').write_text(json.dumps(record, indent=1) + '\n')
    print(json.dumps({k: record[k] for k in ('exit_code', 'result', 'anchor', 'found', 'timing')}))
    return 0 if run.returncode == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
