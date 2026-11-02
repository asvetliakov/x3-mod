#!/usr/bin/env python3
"""Own sun step 3 (FL hidden answer, docs/architecture/own-sun-pass.md section 14): runs the FL native fixture
(verification/probe/fl_sun_occlusion.cpp through verification/analysis/test_fl_sun_occlusion.py build_and_run) unmutated
and with one mutation at a time of the step-3 code in src/engine/fl/sun_occlusion_fl.cpp; each mutation must fail, the
unmutated run must pass with the CPU-state audit clean. The source is restored byte for byte afterwards (also on error).
WSL2 with interop only (native, no Wine, no game). Writes fl_fixture_mutations.json beside this script.
"""
import importlib.util
import json
import re
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
SOURCE = ROOT / 'src/engine/fl/sun_occlusion_fl.cpp'
# name -> (pattern, replacement): exactly one occurrence each
MUTATIONS = {
    'no_hidden_branch': ('    if (own && own_sun_frame_) {', '    if (false && own && own_sun_frame_) {'),
    'answer_ignores_latch': ('    if (own && own_sun_frame_) {', '    if (own) {'),
    'answer_ignores_eligibility': ('    if (own && own_sun_frame_) {', '    if (own_sun_frame_) {'),
    'hidden_answers_visible': ('        own_sun_hidden_frame_ = frame;\n        return core::Hidden;',
                               '        own_sun_hidden_frame_ = frame;\n        return core::Visible;'),
    'hidden_not_recorded': ('        own_sun_hidden_seen_ = true;\n', '\n'),
    'latch_without_request': ('    own_sun_frame_ = patched_ && own_sun_wanted_ && active;',
                              '    own_sun_frame_ = patched_ && active;'),
    'lens_claimed_without_bracket': ('    if (with_lens && !engine_patch::claim_call(lens_site_',
                                     '    if (!engine_patch::claim_call(lens_site_'),
    'early_exit_ignores_latch': ('(!own_sun_frame_ && !log_)', '(!log_)'),
    'latch_while_unpatched': ('    own_sun_frame_ = patched_ && own_sun_wanted_ && active;',
                              '    own_sun_frame_ = own_sun_wanted_ && active;'),
}
spec = importlib.util.spec_from_file_location('test_fl_sun_occlusion',
                                              ROOT / 'verification/analysis/test_fl_sun_occlusion.py')
test = importlib.util.module_from_spec(spec)
spec.loader.exec_module(test)


def run():
    with tempfile.TemporaryDirectory(prefix='x3-fl-sun-step3-') as tmp:
        outcome = test.build_and_run(tmp)
    if outcome is None:
        return None
    result, audit = outcome
    m = re.search(r'^RESULT (\w+) checks=(\d+) failures=(\d+)', result.stdout, re.M)
    timing = next((l for l in result.stdout.splitlines() if l.startswith('TIMING ')), None)
    first = [l[5:] for l in result.stdout.splitlines() if l.startswith('FAIL ')][:2]
    return {'result': m.group(1) if m else None, 'checks': int(m.group(2)) if m else None,
            'failures': int(m.group(3)) if m else None, 'audit_exit': audit.returncode, 'timing': timing,
            'first_failures': first}


def main():
    original = SOURCE.read_bytes()
    text = original.decode()
    out = {}
    try:
        for name, (old, new) in MUTATIONS.items():
            if text.count(old) != 1:
                raise SystemExit(f'{name}: pattern found {text.count(old)} times, expected 1')
            SOURCE.write_text(text.replace(old, new))
            out[name] = run()
            SOURCE.write_bytes(original)
        out['unmutated'] = run()
    finally:
        SOURCE.write_bytes(original)
    (HERE / 'fl_fixture_mutations.json').write_text(json.dumps(out, indent=1) + '\n')
    for k, v in out.items():
        print(k, v and v['result'], v and v['checks'], v and v['failures'], v and v['audit_exit'])
    ok = all(v and v['result'] == 'FAIL' for k, v in out.items() if k != 'unmutated') and \
        out['unmutated'] and out['unmutated']['result'] == 'PASS' and out['unmutated']['audit_exit'] == 0
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
