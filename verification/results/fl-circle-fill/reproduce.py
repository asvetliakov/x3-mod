#!/usr/bin/env python3
"""Producer of verification/results/fl-circle-fill/summary.json (docs/verification/farnhams-legacy.md,
"circle_fill_fix on FL (host evidence, 2026-11-03)"): the site verifier on X3FL.exe, the native fixture (built from the
production sources, run through WSL interop) with the Python model differential of test_fl_circle_fill.py, and the
static x87/XMM walk of the built FL DLL. No Wine, no game launch.

usage: python3 verification/results/fl-circle-fill/reproduce.py [X3FL.exe] [build-fl/d3d9.dll]
"""
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path[:0] = [str(ROOT / 'verification/probe'), str(ROOT / 'verification/analysis')]
import verify_circle_fill_site_fl as verifier  # noqa: E402
import test_fl_circle_fill as t  # noqa: E402


def main():
    exe = Path(sys.argv[1]) if len(sys.argv) > 1 else t.EXE
    dll = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / 'build-fl/d3d9.dll'
    v = verifier.verify(exe)
    summary = {'exe_sha256': v['sha256'], 'verifier': {
        'result': v['result'], 'checks': len(v['checks']), 'failed': v['failed'],
        'windows': {k: w['fnv1a'] for k, w in v['windows'].items()}, 'mutation_guards': v['mutation_guards'],
        'direct_callers': v['direct_callers'], 'directed_rounding_bounds': v['directed_rounding_bounds']}}
    with tempfile.TemporaryDirectory(prefix='x3-fl-circle-') as tmp:
        fixture = t.build(tmp)
        run = subprocess.run([str(fixture), t.windows_path(exe)], capture_output=True, text=True, timeout=600)
    out = run.stdout
    runs = t.parse_runs(out)
    plain = matched = records = 0
    for r in runs:
        if r['tag'] != 'plain':
            continue
        plain += 1
        _, spans = t.expected(r)
        if (r['records'], r['hash']) == (len(spans), t.records_hash(spans)):
            matched += 1
            records += r['records']
    pick = lambda pattern: (re.search(pattern, out, re.M) or [None])[0]  # noqa: E731
    summary['fixture'] = {
        'returncode': run.returncode, 'result': pick(r'^RESULT .*$'), 'flips': pick(r'^FLIPS .*$'),
        'faults': pick(r'^FAULTS .*$'), 'mxcsr': pick(r'^MXCSR .*$'), 'canary': pick(r'^CANARY .*$'),
        'summary': pick(r'^SUMMARY .*$'), 'run_lines': len(runs), 'regs_ok': sum(r['regs'] == 'ok' for r in runs),
        'model_plain_runs': plain, 'model_matched_runs': matched, 'model_records': records,
        'routes': sorted({r['route'] for r in runs}), 'wide_rows': len(re.findall(r'^LOG circle_fill_wide ', out, re.M))}
    if dll.exists():
        walk = subprocess.run([sys.executable, str(HERE / 'walk_wide.py'), str(dll)], capture_output=True, text=True)
        w = json.loads(walk.stdout)
        cache = dll.parent / 'CMakeCache.txt'
        build_type = re.search(r'^CMAKE_BUILD_TYPE:STRING=(.*)$', cache.read_text(), re.M).group(1) if cache.exists() else '?'
        summary['dll_walk'] = {'result': w['result'], 'build_type': build_type, 'walked': len(w['walked']),
                               'x87_users': w['x87_users'], 'xmm_users': w['xmm_users']}
    (HERE / 'summary.json').write_text(json.dumps(summary, indent=1, sort_keys=True) + '\n')
    print(json.dumps(summary, indent=1, sort_keys=True))
    ok = v['result'] == 'PASS' and run.returncode == 0 and plain == matched
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
