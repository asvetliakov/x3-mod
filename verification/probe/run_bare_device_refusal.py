#!/usr/bin/env python3
"""Bare-route refusal on the native host (docs/verification/engine-effects.md, "Native fixture fault").

Builds bare_device_refusal_fixture.exe with the production flags and runs it natively (native_host.py: WSL interop,
the system d3d9.dll and GPU) next to the production proxy, the CMake build's d3d9.dll copied in as the app-local
d3d9.dll. Two modes, each its own process and session log:
  bare       X3M_OWNERSHIP=0: capture.cpp's create_device returns the native device unhooked: none of its 119 documented
             slot targets lies in the proxy image, before and after Reset (the counts in the system d3d9.dll and elsewhere are
             recorded);
             SetRenderTarget(0, back buffer) and the frame, Reset and release calls return success; the session log
             carries exactly one `device_hook route=bare refused reason=vtable_copy_unsafe` row and no device_hooked
             row. (The device's vtable pointer lies outside every image on Windows unhooked as well, so the table's
             location is recorded, not checked.)
  ownership  X3M_OWNERSHIP=1, the control: the device is the ownership wrapper (ownership_factory mode=wrapped), hooked
             (all 119 slot targets in the proxy image, one device_hooked row, no refusal row), and the same calls
             succeed.
X3M_CONFIG=bare otherwise (fixture_log.py). The record goes to verification/results/native-windows/
bare-device-refusal/summary.json with the host block, the DLL and fixture hashes and the checkout's commit. Run it
through wine_lock.py; never launches the game. --cmake-build DIR names the proxy's CMake build directory (default
build/, built here incrementally unless --no-build).
"""
import argparse
import datetime
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

import fixture_log
import native_host
from game_guard import game_running

ROOT = Path(__file__).resolve().parents[2]
PROBE = ROOT / 'verification/probe'
BUILD = PROBE / 'build/bare-device-refusal'
SOURCE = PROBE / 'bare_device_refusal_fixture.cpp'
EXE = BUILD / 'bare_device_refusal_fixture.exe'
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-msse2', '-mfpmath=sse', '-mstackrealign',
         '-mincoming-stack-boundary=2']
MODES = {'bare': '0', 'ownership': '1'}
REFUSED = 'device_hook route=bare refused reason=vtable_copy_unsafe'
# The calls the fixture makes, in order, each of which must succeed (CALL rows).
CALLS = ('CreateDevice', 'GetBackBuffer', 'GetDepthStencilSurface', 'SetRenderTarget', 'SetDepthStencilSurface',
         'SetViewport', *(('Clear', 'BeginScene', 'SetRenderTarget', 'EndScene', 'Present') * 3), 'Reset',
         'GetBackBuffer', 'SetRenderTarget')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def commit():
    def git(*args):
        return subprocess.run(['git', '-C', str(ROOT), *args], capture_output=True, text=True, check=True).stdout
    return git('rev-parse', 'HEAD').strip() + ('-dirty' if git('status', '--porcelain', '--untracked-files=no').strip() else '')


def build(cmake_build):
    BUILD.mkdir(parents=True, exist_ok=True)
    log = BUILD / 'build.log'
    with log.open('w') as out:
        for command in (['cmake', '--build', str(cmake_build), '-j4'],
                        ['i686-w64-mingw32-g++', *FLAGS, '-static', '-static-libgcc', '-static-libstdc++', str(SOURCE),
                         '-o', str(EXE), '-luser32']):
            subprocess.run(command, cwd=ROOT, check=True, stdout=out, stderr=subprocess.STDOUT)
    return {'log': str(log.relative_to(ROOT)), 'warnings': len(re.findall(r'\bwarning:', log.read_text(errors='replace')))}


def run(mode, dll, timeout):
    directory = BUILD / f'{mode}-{datetime.datetime.now():%Y%m%d-%H%M%S-%f}'
    directory.mkdir(parents=True)
    shutil.copy(EXE, directory)
    shutil.copy(dll, directory / 'd3d9.dll')
    log_env = fixture_log.session_log_env(directory)
    log_env['X3M_LOG_FILE'] = native_host.windows_path(Path(log_env['X3M_LOG_FILE'][2:]))
    try:
        done, stdout = native_host.run_native(directory / EXE.name, [], directory, timeout,
                                              env=dict(X3M_OWNERSHIP=MODES[mode], **log_env))
        exit_code = done.returncode
    except subprocess.TimeoutExpired as error:
        exit_code, stdout = 'timeout', (error.stdout or b'').decode('utf-8', 'replace').replace('\r\n', '\n')
    (directory / 'stdout.txt').write_text(stdout)
    logs = sorted((directory / 'x3-modern-captures').glob('session-*.log'))
    session = logs[-1].read_text(errors='replace') if logs else ''
    return dict(directory=directory, exit=exit_code, stdout=stdout, session=session)


def row_value(stdout, tag):
    for line in stdout.splitlines():
        head, _, rest = line.partition(' ')
        if head == tag:
            return rest.strip()
    return None


def validate(mode, r):
    """(problems, summary) of one run."""
    stdout, session = r['stdout'], r['session']
    calls = re.findall(r'^CALL (\w+) hr=([0-9a-f]{8})$', stdout, re.M)
    vtable, vtable_after = row_value(stdout, 'VTABLE'), row_value(stdout, 'VTABLE_AFTER')
    slot_rows = {tag: {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', row_value(stdout, tag) or '')}
                 for tag in ('SLOTS', 'SLOTS_AFTER')}
    system = row_value(stdout, 'D3D9_MODULE')
    proxy = row_value(stdout, 'PROXY_MODULE')
    lines = session.splitlines()
    refused = [line for line in lines if REFUSED in line]
    hooked = [line for line in lines if re.search(r'(^|\s)device_hooked\s', line)]
    factory = [m.group(1) for line in lines for m in [re.search(r'ownership_factory mode=(\w+)', line)] if m]
    summary = {'exit': r['exit'], 'calls': len(calls), 'failed_calls': [c for c in calls if int(c[1], 16) & 0x80000000],
               'vtable': vtable, 'vtable_after': vtable_after, 'slots': slot_rows, 'system_d3d9': system, 'proxy': proxy,
               'refused_rows': len(refused), 'device_hooked_rows': len(hooked), 'ownership_factory': factory,
               'result': row_value(stdout, 'RESULT'), 'exception': row_value(stdout, 'EXCEPTION'),
               'session_log_rows': len(lines)}
    problems = []

    def need(condition, what):
        if not condition:
            problems.append(f'{mode}: {what}')
    need(r['exit'] == 0, f'exit {r["exit"]}')
    need(summary['exception'] is None, f'exception {summary["exception"]}')
    need(tuple(name for name, _ in calls) == CALLS, f'call sequence {[name for name, _ in calls]}')
    need(not summary['failed_calls'], f'failed calls {summary["failed_calls"]}')
    need(summary['result'] == 'PASS failures=0', f'result {summary["result"]}')
    need(bool(lines), 'no session log')
    need(system is not None and proxy is not None and system.lower() != proxy.lower(), 'module rows')
    if mode == 'bare':
        for tag, counts in slot_rows.items():
            need(counts.get('proxy') == 0 and counts.get('system', 0) + counts.get('other', 0) == 119,
                 f'{tag} {counts} (expected no slot in the proxy)')
        need(len(refused) == 1, f'{len(refused)} refusal rows')
        need(not hooked, f'{len(hooked)} device_hooked rows')
        need(not factory, f'ownership_factory rows {factory}')
    else:
        for tag, counts in slot_rows.items():
            need(counts == {'proxy': 119, 'system': 0, 'other': 0}, f'{tag} {counts} (expected every slot in the proxy)')
        need(factory == ['wrapped'], f'ownership_factory {factory}')
        need(len(hooked) == 1, f'{len(hooked)} device_hooked rows')
        need(not refused, f'{len(refused)} refusal rows')
    return problems, summary


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--timeout', type=int, default=120)
    parser.add_argument('--cmake-build', default='build', metavar='DIR')
    args = parser.parse_args()
    if native_host.missing():
        raise SystemExit('--host native needs ' + '; '.join(native_host.missing()))
    if game_running() or native_host.game_running_windows():
        raise SystemExit('the game is running')
    dll = ROOT / args.cmake_build / 'd3d9.dll'
    record = {'host_kind': 'native', 'commit': commit(), 'game_launched': False}
    if not args.no_build:
        record['build'] = build(args.cmake_build)
    record['binaries'] = {'fixture_sha256': sha(EXE), 'dll_sha256': sha(dll), 'cmake_build': args.cmake_build,
                          'capture_cpp_sha256': sha(ROOT / 'src/proxy/capture.cpp')}
    problems, runs = [], {}
    for mode in MODES:
        r = run(mode, dll, args.timeout)
        if 'host' not in record:
            record['host'] = native_host.identity(r['stdout'])
        p, summary = validate(mode, r)
        summary['directory'] = str(r['directory'].relative_to(ROOT))
        runs[mode] = summary
        problems += p
        print(f'{mode}: exit={summary["exit"]} calls={summary["calls"]} vtable={summary["vtable"]} slots={summary["slots"]["SLOTS"]} '
              f'refused={summary["refused_rows"]} hooked={summary["device_hooked_rows"]} problems={len(p)}', flush=True)
    record.update(runs=runs, problems=problems, passed=not problems)
    results = native_host.results_dir(ROOT, 'bare-device-refusal')
    (results / 'summary.json').write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps({'passed': record['passed'], 'problems': problems, 'results': str(results.relative_to(ROOT))}, indent=1))
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
