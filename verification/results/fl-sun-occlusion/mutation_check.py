#!/usr/bin/env python3
"""Mutation check of the FL sun occlusion hooks fixture (docs/verification/farnhams-legacy.md, "Step 9").

Applies one mutation at a time to src/engine/fl/sun_occlusion_fl.cpp: every XmmSave envelope removed (probe handler,
lens begin, lens end), both PreserveCpuState envelopes removed (lens begin/end), and the gate-1 flags word read at
AP's +0xfc instead of +0xec; rebuilds and runs the native fixture through the host test's build_and_run
(verification/analysis/test_fl_sun_occlusion.py) and records the fixture's failure count and the audit's exit per
mutation. The source is restored byte for byte afterwards (also on error). Each mutation must fail; the unmutated
run must pass. WSL2 with interop only. Prints one JSON object and writes mutation_check.json beside this script.
"""
import importlib.util
import json
import re
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = ROOT / 'src/engine/fl/sun_occlusion_fl.cpp'
# name -> (pattern, replacement, occurrences)
MUTATIONS = {'no_xmm_save': ('    XmmSave xmm;\n', '    // XmmSave xmm;\n', 3),
             'no_preserve_cpu_state': ('    x3m::PreserveCpuState cpu;\n', '    // x3m::PreserveCpuState cpu;\n', 2),
             'flags_offset_0xfc': ('word(reinterpret_cast<const std::uint32_t*>(config), L::config_video_flags)',
                                   'word(reinterpret_cast<const std::uint32_t*>(config), 0xfc)', 1)}
spec = importlib.util.spec_from_file_location('test_fl_sun_occlusion',
                                              ROOT / 'verification/analysis/test_fl_sun_occlusion.py')
test = importlib.util.module_from_spec(spec)
spec.loader.exec_module(test)


def run():
    with tempfile.TemporaryDirectory(prefix='x3-fl-sun-mut-') as tmp:
        outcome = test.build_and_run(tmp)
    if outcome is None:
        return None
    result, audit = outcome
    m = re.search(r'^RESULT (\w+) checks=(\d+) failures=(\d+)', result.stdout, re.M)
    first = [l[5:] for l in result.stdout.splitlines() if l.startswith('FAIL ')][:2]
    return {'result': m.group(1) if m else None, 'checks': int(m.group(2)) if m else None,
            'failures': int(m.group(3)) if m else None, 'audit_exit': audit.returncode, 'first_failures': first}


def main():
    original = SOURCE.read_bytes()
    text = original.decode()
    out = {}
    try:
        for name, (old, new, count) in MUTATIONS.items():
            if text.count(old) != count:
                raise SystemExit(f'{name}: pattern found {text.count(old)} times, expected {count}')
            SOURCE.write_text(text.replace(old, new))
            out[name] = run()
            SOURCE.write_bytes(original)
        out['unmutated'] = run()
    finally:
        SOURCE.write_bytes(original)
    print(json.dumps(out))
    (HERE / 'mutation_check.json').write_text(json.dumps(out) + '\n')
    ok = all(v and v['result'] == 'FAIL' for k, v in out.items() if k != 'unmutated') and \
        out['unmutated'] and out['unmutated']['result'] == 'PASS'
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
