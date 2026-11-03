#!/usr/bin/env python3
"""Builds and runs the FL collision-trio host evidence: the three Capstone verifiers on the installed X3FL.exe and the
three native fixtures (MinGW i686, -mstackrealign -mincoming-stack-boundary=2 -msse2 -mfpmath=sse, natively through WSL
interop, no Wine) exactly as their host tests build them. Writes results.json beside this script: verifier check counts,
fixture check counts and the fixtures' summary rows (DIFFERENTIAL per site and MXCSR mode, VERIFY, BENCH; no raw log, no
game bytes). Exit 0 = all PASS.

usage: python3 verification/results/fl-collision/run_fixtures.py
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
sys.path.insert(0, str(ROOT / 'verification/probe'))
import verify_collide_sat_fl as sat_sites  # noqa: E402
import verify_collide_memo_sites_fl as memo_sites  # noqa: E402
import verify_collide_box_cull_sites_fl as box_sites  # noqa: E402
import test_fl_collide_box_cull as box  # noqa: E402
import test_fl_collide_memo as memo  # noqa: E402
import test_fl_collide_sat as sat  # noqa: E402

SUMMARY = ('DIFFERENTIAL', 'VERIFY', 'BENCH', 'IMAGE')


def fixture(module, prefix):
    with tempfile.TemporaryDirectory(prefix=prefix) as tmp:
        exe = module.build(tmp)
        if not exe or not module.EXE.is_file():
            return {'result': 'SKIP'}
        win = subprocess.run(['wslpath', '-w', str(module.EXE)], capture_output=True, text=True, check=True).stdout.strip()
        start = time.monotonic()
        run = subprocess.run([str(exe), win], text=True, capture_output=True, timeout=900)
        elapsed = round(time.monotonic() - start, 1)
    out = run.stdout
    m = re.search(r'RESULT (\w+) checks=(\d+) failures=(\d+)', out)
    return {'exit': run.returncode, 'result': m.group(1) if m else 'FAIL',
            'checks': int(m.group(2)) if m else 0, 'failures': int(m.group(3)) if m else -1, 'run_s': elapsed,
            'summary': [l for l in out.splitlines() if l.startswith(SUMMARY)],
            'failed': [l[:200] for l in out.splitlines() if l.startswith('FAIL')][:20]}


def main():
    report = {'exe': str(box.EXE)}
    data = box.EXE.read_bytes() if box.EXE.is_file() else None
    for name, mod in (('verify_collide_sat_fl', sat_sites), ('verify_collide_memo_sites_fl', memo_sites),
                      ('verify_collide_box_cull_sites_fl', box_sites)):
        if data is None:
            report[name] = {'result': 'SKIP'}
            continue
        r = mod.verify(data)
        report[name] = {'result': r['result'], 'sha256': r['exe_info']['sha256'], 'checks': len(r['checks']),
                        'failed': [k for k, v in r['checks'].items() if not v['ok']]}
    report['fl_collide_sat'] = fixture(sat, 'x3-fl-sat-')
    report['fl_collide_box_cull'] = fixture(box, 'x3-fl-box-')
    report['fl_collide_memo'] = fixture(memo, 'x3-fl-memo-')
    (HERE / 'results.json').write_text(json.dumps(report, indent=1) + '\n')
    ok = all(report[k]['result'] == 'PASS' for k in report if k != 'exe')
    for k in report:
        if k != 'exe':
            v = report[k]
            print(k, v['result'], v.get('checks', ''))
            for line in v.get('summary', []):
                print('   ', line)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
