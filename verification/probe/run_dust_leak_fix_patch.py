#!/usr/bin/env python3
"""Run the dust-scene leak fix fixture in the X3 bottle and write a compact record.

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_dust_leak_fix_patch.py

Builds the fixture first (build_dust_leak_fix_patch.py, host only, into the
untracked build/verification/dust-leak-fix/), runs it once, keeps its stdout
beside the executable and writes verification/results/bottle-X3/dust-leak-fix-patch.json:
bottle, toolchain and build audit, every CHECK with its outcome, every CASE
(iterations, releases, the released node, EDI, [ESP+0x20], registers, ZF) per
step, the protection seen around the write, the arena use, and the
install/restore/hits log rows parsed with verify_dust_leak_fix_site's parsers.
Never launches the game; the game EXE is read only for the window bytes.
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
import build_dust_leak_fix_patch as build
import verify_dust_leak_fix_site as verifier
ROOT = Path(__file__).resolve().parents[2]
NAME = 'dust-leak-fix-patch.json'


PRODUCTION_SOURCES = ('src/proxy/dust_leak_fix.cpp', 'src/proxy/dust_leak_fix.h', 'src/proxy/dust_leak_fix_sites.h',
                      'src/proxy/engine_patch.cpp', 'src/proxy/engine_patch.h')


def source_binding():
    """The checkout's commit and whether the production sources the fixture links differ from it."""
    def git(*args):
        return subprocess.run(['git', '-C', str(ROOT), *args], capture_output=True, text=True, check=True).stdout
    changed = [line[3:] for line in git('status', '--porcelain', '--', *PRODUCTION_SOURCES).splitlines()]
    return {'commit': git('rev-parse', 'HEAD').strip(), 'production_sources_dirty': bool(changed), 'dirty_paths': changed,
            'sha256': {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in PRODUCTION_SOURCES}}


def fields(text):
    return dict(re.findall(r'(\w+)=(\S+)', text))


def parse(stdout):
    report = {'checks': [], 'cases': [], 'memory': [], 'roview': None, 'timing_us': None, 'arena': None,
              'install_rows': [], 'restore_rows': [], 'hits_rows': [], 'result': None}
    for line in stdout.splitlines():
        tag, _, rest = line.partition(' ')
        if tag == 'CHECK':
            outcome, _, tail = rest.partition(' ')
            name, _, detail = tail.partition(' ')
            report['checks'].append({'name': name, 'pass': outcome == 'pass', **({'detail': detail[:300]} if outcome != 'pass' and detail else {})})
        elif tag == 'CASE':
            f = fields(rest)
            report['cases'].append({'step': f['step'], 'case': f['case'], 'ok': f['ok'] == '1',
                                    **{k: f[k] for k in ('iterations', 'releases', 'released', 'edi', 'slot', 'regs', 'zf', 'unlinked', 'zeroed', 'others')}})
        elif tag == 'MEMORY':
            report['memory'].append(fields(rest))
        elif tag == 'ROVIEW':
            report['roview'] = fields(rest)
        elif tag == 'CRASH':
            report['crash'] = fields(rest)
        elif tag == 'TIMING':
            report['timing_us'] = {k: float(v) for k, v in fields(rest).items()}
        elif tag == 'ARENA':
            report['arena'] = {k: int(v) for k, v in fields(rest).items()}
        elif tag == 'RESULT':
            report['result'] = {k: int(v) for k, v in fields(rest).items()}
        elif tag == 'LOG':
            install, restore, hits = verifier.parse_log_line(rest), verifier.parse_restore_line(rest), verifier.parse_hits_line(rest)
            if install:
                report['install_rows'].append({k: (f'{v:08x}' if k in ('site', 'stub') else v) for k, v in install.items()})
            elif restore:
                report['restore_rows'].append({'site': f'{restore["site"]:08x}', 'status': restore['status'],
                                               'found': restore['found'].hex() if restore['found'] is not None else None, 'registered': restore['registered']})
            elif hits:
                report['hits_rows'].append(hits)
    return report


def main():
    name = os.environ.get('X3M_FIXTURE_BOTTLE')
    if name != 'X3':
        sys.exit('set X3M_FIXTURE_BOTTLE=X3 and run through verification/probe/wine_lock.py')
    binding = source_binding()  # taken before the build, so the binary is built from what it names
    built = build.build()
    started = time.time()
    process = subprocess.Popen([bottle.WINE, *bottle.wine_args(name), str(build.EXE)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    timed_out = False
    try:
        stdout, stderr = process.communicate(timeout=120)
    except subprocess.TimeoutExpired:
        timed_out = True
        process.kill()
        # The fixture itself runs under the Wine loader's children; end it by its image name.
        subprocess.run(['pkill', '-f', build.EXE.name], check=False)
        stdout, stderr = process.communicate()
    run = subprocess.CompletedProcess(process.args, 124 if timed_out else process.returncode, stdout, stderr)
    elapsed = round(time.time() - started, 1)
    (build.BUILD / 'dust-leak-fix-patch.stdout').write_text(run.stdout)
    (build.BUILD / 'dust-leak-fix-patch.stderr').write_text(run.stderr)
    report = parse(run.stdout)
    checks = report['checks']
    passed = sum(c['pass'] for c in checks)
    result = report['result'] or {}
    ok = (run.returncode == 0 and bool(checks) and passed == len(checks) and result.get('checks') == len(checks) and result.get('failures') == 0)
    record = {'fixture': 'verification/probe/dust_leak_fix_patch_fixture.cpp', 'runner': 'verification/probe/run_dust_leak_fix_patch.py',
              'production_sources': list(PRODUCTION_SOURCES), 'source': binding,
              'game_launched': False, 'bottle': bottle.describe(name), 'exit_status': run.returncode, 'elapsed_s': elapsed,
              'executable_sha256': hashlib.sha256(build.EXE.read_bytes()).hexdigest(), 'toolchain': built['toolchain'], 'build_audit': built['audit'],
              'check_count': len(checks), 'pass_count': passed, **report, 'passed': ok,
              'note': 'install/restore timings are fixture-inclusive one-off costs, not game FPS'}
    out = bottle.results_dir(ROOT, name) / NAME
    out.write_text(json.dumps(record, indent=1) + '\n')
    print(json.dumps({'checks': len(checks), 'passed': passed, 'exit_status': run.returncode, 'cases': len(report['cases']),
                      'cases_ok': sum(c['ok'] for c in report['cases']), 'hits_rows': report['hits_rows'], 'arena': report['arena'],
                      'failed': [c['name'] for c in checks if not c['pass']], 'record': str(out.relative_to(ROOT))}))
    if not ok:
        print(run.stdout[-3000:], run.stderr[-2000:], file=sys.stderr)
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
