#!/usr/bin/env python3
"""Builds and runs the two FL SETA native fixtures exactly as their host tests do (verification/analysis/
test_fl_seta_manual_flight.py and test_fl_seta_restore.py: MinGW i686, -mstackrealign -mincoming-stack-boundary=2 -msse2
-mfpmath=sse, natively through WSL interop, no Wine) and the two site verifiers on the installed X3FL.exe. Writes
results.json beside this script: counts, summary lines and verifier check flags only (no raw log, no game bytes).
Exit 0 = all PASS.

usage: python3 verification/results/fl-seta/run_fixtures.py
"""
import json
import re
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'verification/analysis'))
sys.path.insert(0, str(ROOT / 'verification/probe'))
import test_fl_seta_manual_flight as smf  # noqa: E402
import test_fl_seta_restore as sr  # noqa: E402
import verify_seta_manual_flight_sites_fl as smf_sites  # noqa: E402
import verify_seta_restore_sites_fl as sr_sites  # noqa: E402

SUMMARY = ('IMAGE', 'FLIPS', 'CONTRACT', 'ARENA', 'CALLS', 'RESULT')
ROWS = ('LOG seta_manual_flight installed=1 mode=2', 'LOG seta_restore installed=1')


def fixture(module, prefix):
    with tempfile.TemporaryDirectory(prefix=prefix) as tmp:
        start = time.monotonic()
        run = module.run_fixture(tmp)
        elapsed = round(time.monotonic() - start, 2)
    if run is None:
        return {'result': 'SKIP'}
    if isinstance(run, dict):
        run = run['']
    out = run.stdout
    m = re.search(r'RESULT (\w+) checks=(\d+) failures=(\d+)', out)
    return {'exit': run.returncode, 'result': m.group(1) if m else 'FAIL', 'checks': int(m.group(2)) if m else 0,
            'failures': int(m.group(3)) if m else -1, 'build_and_run_s': elapsed,
            'summary': [l.split(' base=')[0] if l.startswith('IMAGE') else l for l in out.splitlines()
                        if l.startswith(SUMMARY)],
            'install_row': next((re.sub(r'0x[0-9a-f]{8}', '0x........', l)[:400] for l in out.splitlines()
                                 if l.startswith(ROWS)), None),
            'failed': [l[:200] for l in out.splitlines() if l.startswith('FAIL')][:20]}


def main():
    exe = smf.EXE
    report = {'exe': str(exe)}
    data = exe.read_bytes() if exe.is_file() else None
    for name, probe in (('verify_seta_manual_flight_sites_fl', smf_sites), ('verify_seta_restore_sites_fl', sr_sites)):
        if data is None:
            report[name] = {'result': 'SKIP'}
            continue
        r = probe.verify(data)
        report[name] = {'result': r['result'], 'sha256': r['exe_info']['sha256'],
                        'checks': {k: v['ok'] for k, v in r['checks'].items()},
                        'windows': r['checks']['windows']['count'],
                        'anchors': r['checks']['anchors']['count']}
    report['fl_seta_manual_flight'] = fixture(smf, 'x3-fl-smf-')
    report['fl_seta_restore'] = fixture(sr, 'x3-fl-seta-')
    # init orders: the chase family's masked install rows alone, after a loader-order SETA attach, after an out-of-order
    # SETA initialize (refused chase_pending, then attached)
    with tempfile.TemporaryDirectory(prefix='x3-fl-seta-order-') as tmp:
        runs = sr.run_fixture(tmp, sr.ORDER_MODES)
    if runs is None:
        report['init_orders'] = {'result': 'SKIP'}
    else:
        rows = {m: [l for l in r.stdout.splitlines() if l.startswith('CHASE_ROW ')] for m, r in runs.items()}
        same = rows['chase_first'] == rows['chase_only'] and rows['seta_first'] == rows['chase_only']
        passed = all(r.returncode == 0 for r in runs.values())
        report['init_orders'] = {
            'result': 'PASS' if same and passed else 'FAIL',
            'chase_rows_identical': same, 'chase_rows': len(rows['chase_only']),
            'checks': {m: re.search(r'RESULT \w+ checks=\d+ failures=\d+', r.stdout).group(0) for m, r in runs.items()},
            'chase_install_row': next((l[10:130] for l in rows['chase_only'] if 'chase_transition installed=' in l), None)}
    (HERE / 'results.json').write_text(json.dumps(report, indent=1) + '\n')
    ok = all(report[k]['result'] == 'PASS' for k in report if k != 'exe')
    for k in report:
        if k != 'exe':
            v = report[k]
            checks = v.get('checks')
            print(k, v['result'], checks if isinstance(checks, int) else
                  sum(bool(c) for c in (checks or {}).values()), v.get('failures', ''))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
