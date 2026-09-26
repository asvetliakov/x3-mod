#!/usr/bin/env python3
"""Run the run-in-background patch fixture in the X3 bottle and write a compact record.

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_run_in_background_patch.py

Builds the fixture first (build_run_in_background_patch.py, host only, into the untracked
build/verification/run-in-background-patch/), runs it once, keeps its stdout beside the executable and writes
verification/results/bottle-X3/run-in-background-patch.json: bottle, toolchain and build audit, the commit plus the
SHA-256 of every production source the fixture links (so the record stays checkable before the change is committed),
every CHECK with its outcome, the vanilla and patched register records per scenario and the run_in_background rows
parsed with verify_run_in_background_site's parser. Never launches the game; the game EXE is not read.
"""
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path
import bottle
import build_run_in_background_patch as build
import verify_run_in_background_site as verifier
ROOT = Path(__file__).resolve().parents[2]
NAME = 'run-in-background-patch.json'
PRODUCTION_SOURCES = ('src/proxy/run_in_background.cpp', 'src/proxy/run_in_background.h', 'src/proxy/run_in_background_sites.h',
                      'src/proxy/engine_patch.cpp', 'src/proxy/engine_patch.h')


def source_binding():
    """The checkout's commit, whether the linked production sources differ from it, and their content hashes."""
    def git(*args):
        return subprocess.run(['git', '-C', str(ROOT), *args], capture_output=True, text=True, check=True).stdout
    changed = [line[3:] for line in git('status', '--porcelain', '--', *PRODUCTION_SOURCES).splitlines()]
    return {'commit': git('rev-parse', 'HEAD').strip(), 'production_sources_dirty': bool(changed), 'dirty_paths': changed,
            'sha256': {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in PRODUCTION_SOURCES}}


def parse(stdout):
    report = {'checks': [], 'runs': [], 'rows': [], 'result': None}
    for line in stdout.splitlines():
        tag, _, rest = line.partition(' ')
        if tag == 'CHECK':
            outcome, _, tail = rest.partition(' ')
            name, _, detail = tail.partition(' ')
            report['checks'].append({'name': name, 'pass': outcome == 'pass', **({'detail': detail[:200]} if outcome != 'pass' and detail else {})})
        elif tag == 'RUN':
            report['runs'].append(dict(re.findall(r'(\w+)=(\S+)', rest)))
        elif tag == 'RESULT':
            report['result'] = {k: int(v) for k, v in re.findall(r'(\w+)=(\S+)', rest)}
        elif tag == 'LOG':
            row = verifier.parse_log_line(rest)
            if row:
                report['rows'].append({k: (f'{v:#010x}' if k in ('site', 'handler', 'flags_before', 'flags_after') and v is not None else v)
                                       for k, v in row.items()})
    return report


def main():
    name = os.environ.get('X3M_FIXTURE_BOTTLE')
    if name != 'X3':
        sys.exit('set X3M_FIXTURE_BOTTLE=X3 and run through verification/probe/wine_lock.py')
    binding = source_binding()  # taken before the build, so the binary is built from what it names
    built = build.build()
    started = time.time()
    env = {k: v for k, v in os.environ.items() if k != 'X3M_RUN_IN_BACKGROUND'}  # the fixture sets it per case
    process = subprocess.Popen([bottle.WINE, *bottle.wine_args(name), str(build.EXE)], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, env=env)
    timed_out = False
    try:
        stdout, stderr = process.communicate(timeout=120)
    except subprocess.TimeoutExpired:
        timed_out = True
        process.kill()
        subprocess.run(['pkill', '-f', build.EXE.name], check=False)
        stdout, stderr = process.communicate()
    returncode = 124 if timed_out else process.returncode
    elapsed = round(time.time() - started, 1)
    (build.BUILD / 'run-in-background-patch.stdout').write_text(stdout)
    (build.BUILD / 'run-in-background-patch.stderr').write_text(stderr)
    report = parse(stdout)
    checks = report['checks']
    passed = sum(c['pass'] for c in checks)
    result = report['result'] or {}
    ok = returncode == 0 and bool(checks) and passed == len(checks) and result.get('checks') == len(checks) and result.get('failures') == 0
    record = {'fixture': 'verification/probe/run_in_background_patch_fixture.cpp', 'runner': 'verification/probe/run_run_in_background_patch.py',
              'production_sources': list(PRODUCTION_SOURCES), 'source': binding, 'game_launched': False, 'bottle': bottle.describe(name),
              'exit_status': returncode, 'elapsed_s': elapsed, 'executable_sha256': hashlib.sha256(build.EXE.read_bytes()).hexdigest(),
              'toolchain': built['toolchain'], 'build_audit': built['audit'], 'check_count': len(checks), 'pass_count': passed, **report,
              'passed': ok}
    out = bottle.results_dir(ROOT, name) / NAME
    out.write_text(json.dumps(record, indent=1) + '\n')
    print(json.dumps({'checks': len(checks), 'passed': passed, 'exit_status': returncode, 'rows': len(report['rows']),
                      'failed': [c['name'] for c in checks if not c['pass']], 'record': str(out.relative_to(ROOT))}))
    if not ok:
        print(stdout[-3000:], stderr[-2000:], file=sys.stderr)
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
