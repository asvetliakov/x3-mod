#!/usr/bin/env python3
"""Mutation check of the FL scene-end hook fixture (docs/verification/farnhams-legacy.md, "Step 5").

Comments out one CPU-state envelope in src/engine/fl/scene_hook_fl.cpp at a time (the XMM0-7 save, then
PreserveCpuState), rebuilds and runs the native fixture through the host test's build_and_run
(verification/analysis/test_fl_scene_hook.py) and prints the fixture's failure count per mutation; the source is
restored byte for byte afterwards (also on error). fixture.json is not touched. Each mutation must fail; the
unmutated run must pass. WSL2 with interop only. Prints one JSON object.
"""
import importlib.util
import json
import re
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = ROOT / 'src/engine/fl/scene_hook_fl.cpp'
MUTATIONS = {'no_xmm_save': ('    XmmSave xmm;\n', '    // XmmSave xmm;\n'),
             'no_preserve_cpu_state': ('    x3m::PreserveCpuState cpu;\n', '    // x3m::PreserveCpuState cpu;\n')}
spec = importlib.util.spec_from_file_location('test_fl_scene_hook', ROOT / 'verification/analysis/test_fl_scene_hook.py')
test = importlib.util.module_from_spec(spec)
spec.loader.exec_module(test)


def run():
    with tempfile.TemporaryDirectory(prefix='x3-fl-scene-mut-') as tmp:
        outcome = test.build_and_run(tmp)
    if outcome is None:
        return None
    result, audit = outcome
    m = re.search(r'^RESULT (\w+) checks=(\d+) failures=(\d+)', result.stdout, re.M)
    return {'result': m.group(1) if m else None, 'checks': int(m.group(2)) if m else None,
            'failures': int(m.group(3)) if m else None, 'audit_exit': audit.returncode}


def main():
    original = SOURCE.read_bytes()
    text = original.decode()
    out = {}
    try:
        for name, (old, new) in MUTATIONS.items():
            if text.count(old) != 1:
                raise SystemExit(f'{name}: pattern not found exactly once')
            SOURCE.write_text(text.replace(old, new))
            out[name] = run()
            SOURCE.write_bytes(original)
        out['unmutated'] = run()
    finally:
        SOURCE.write_bytes(original)
    print(json.dumps(out))
    ok = all(v and v['result'] == 'FAIL' for k, v in out.items() if k != 'unmutated') and \
        out['unmutated'] and out['unmutated']['result'] == 'PASS'
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
