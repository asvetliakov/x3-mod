#!/usr/bin/env python3
"""Real-D3D capture lifetime fixture in X3; consumes --dll, never builds production.

Run with X3M_FIXTURE_BOTTLE=X3 through verification/probe/wine_lock.py.
Uses a private scratch directory and process-local d3d9 override. No game launch
or installed DLL/bottle configuration changes. Native Windows is not verified.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
import bottle
from game_guard import game_running
from capture_bloom_x3_build import build
ROOT = Path(__file__).resolve().parents[2]
CASES = ('normal', 'get_device', 'final_release', 'thread_final_release', 'reset',
         'reset_fail', 'reset_ex', 'reset_ex_fail', 'escape', 'continue')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fields(line):
    return dict(re.findall(r'(\w+)=(\S+)', line))


def parse_output(output):
    """Fail closed on missing/duplicate cases and contradictory seam evidence."""
    lines = output.splitlines()
    if any(line.startswith(('CHECK_FAIL ', 'API_FAIL ')) for line in lines):
        raise ValueError('Fixture reported failed checks')
    terminals = [line for line in lines if line.startswith('RESULT ')]
    if len(terminals) != 1 or not terminals[0].startswith('RESULT PASS '):
        raise ValueError('Missing unique RESULT PASS')
    terminal = fields(terminals[0])
    if int(terminal['failures']) != 0 or int(terminal['checks']) <= 0:
        raise ValueError('Invalid final checks')
    result = {}
    skipped = 0
    for line in lines:
        if not line.startswith('CASE '):
            continue
        row = fields(line)
        name = row.get('name')
        if name not in CASES or name in result:
            raise ValueError('Unknown/duplicate fixture case: ' + str(name))
        result[name] = row
        if row.get('status') == 'SKIP':
            if name not in ('reset_ex', 'reset_ex_fail') or row.get('reason') != 'create9ex_unavailable' or int(row['hr'], 16) != 0x8876086a:
                raise ValueError('Unexpected skip: ' + name)
            skipped += 1
            continue
        if row.get('status') != 'PASS':
            raise ValueError('Case failed: ' + name)
        reset = name.startswith('reset')
        expected = dict(pre=1, post=int(name != 'escape'), cleanup=1,
                        abnormal=int(name == 'escape'), prepared=1,
                        committed=int(not reset and name != 'escape'),
                        resets=int(reset), defaults_clear=int(reset), map_absent=1,
                        cpu_expired=1, pin_kept=int(reset), active=0, original=1,
                        caught=int(name == 'escape'), continued=int(name == 'continue'),
                        resumed=int(name == 'continue'))
        for key, value in expected.items():
            if int(row[key]) != value:
                raise ValueError(f'{name}: {key}={row[key]}, expected {value}')
        if int(row['motion_refs']) <= 0 or int(row['hdr']) != 1 or int(row['taa']) != 1:
            raise ValueError(name + ': persistent MotionOutput/HDR resources were not active at pre')
        if int(row['prepare_hr'], 16) & 0x80000000:
            raise ValueError(name + ': failed actual prepare HRESULT')
        if expected['committed'] and int(row['commit_hr'], 16) & 0x80000000:
            raise ValueError(name + ': failed actual commit HRESULT')
        if reset and bool(int(row['reset_hr'], 16) & 0x80000000) != name.endswith('_fail'):
            raise ValueError(name + ': Reset result disagrees with scenario')
        final_during_original = name in ('final_release', 'thread_final_release')
        if (int(row['release']) > 0) != final_during_original:
            raise ValueError(name + ': native Release count contradicts lifetime')
        if final_during_original and (int(row['during_absent']) != 0 or int(row['during_expired']) != 0):
            raise ValueError(name + ': context died during original')
    if set(result) != set(CASES):
        raise ValueError('Missing cases: ' + repr(set(CASES) - set(result)))
    if int(terminal['cases']) != len(CASES) - skipped or int(terminal['skipped']) != skipped:
        raise ValueError('Final case count mismatch')
    return {'checks': int(terminal['checks']), 'cases': result, 'skipped': skipped}


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', required=True, type=Path, help='existing X3M_MOTION_OUTPUT_FIXTURE DLL')
    parser.add_argument('--exe', type=Path, help='reuse explicit EXE; otherwise build this standalone fixture only')
    parser.add_argument('--ownership', choices=('both', '0', '1'), default='both')
    args = parser.parse_args(argv)
    args.dll = args.dll.expanduser().resolve()
    if not args.dll.is_file():
        parser.error('Selected DLL does not exist')
    if args.exe:
        args.exe = args.exe.expanduser().resolve()
        if not args.exe.is_file():
            parser.error('Selected EXE does not exist')
    return args


def main(argv=None):
    args = parse_args(argv)
    if bottle.BOTTLE != 'X3':
        raise RuntimeError('Set X3M_FIXTURE_BOTTLE=X3 explicitly; no routine Steam run')
    if game_running():
        raise RuntimeError('Game active or process inventory failed; postpone')
    results = bottle.results_dir(ROOT)
    summary = results / 'capture-bloom-x3-summary.json'
    report = {'status': 'RUNNING', 'passed': False, 'bottle': bottle.describe(),
              'game_launched': False, 'native_windows_verified': False,
              'dll_path': str(args.dll), 'dll_sha256': sha(args.dll), 'modes': {},
              'limits': ['Synthetic owner and original; actual capture bridge, GPU preparation/commit, Release and Reset hooks.',
                         'Exceptions raised only after a normal D3D hook return; no arbitrary injected-COM SEH recovery claim.',
                         'Actual persistent MotionOutput/HDR shader resources combine with BloomPass ownership. TAA is enabled, but lazy temporal history/resolve resources are not created or qualified.',
                         'ResetEx may be skipped only when Direct3DCreate9Ex reports unavailable.']}
    def save():
        summary.write_text(json.dumps(report, indent=2) + '\n')
    save()
    try:
        directory = ROOT / 'verification/probe/build' / ('capture-bloom-x3-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
        directory.mkdir(parents=True)
        exe = args.exe
        if not exe:
            exe, command = build(directory / 'compile')
            report['fixture_build_command'] = command
        report['exe_sha256'] = sha(exe)
        for mode in (('0', '1') if args.ownership == 'both' else (args.ownership,)):
            run_dir = directory / ('ownership-' + mode)
            run_dir.mkdir()
            shutil.copy2(exe, run_dir / exe.name)
            shutil.copy2(args.dll, run_dir / 'd3d9.dll')
            if sha(run_dir / 'd3d9.dll') != report['dll_sha256']:
                raise RuntimeError('Selected DLL changed before run')
            # Make fixture isolation independent of renderer settings inherited
            # from a user's shell. This affects only the spawned process.
            env = {k: v for k, v in os.environ.items() if not k.startswith('X3M_')}
            env.update(X3M_FIXTURE_BOTTLE='X3', X3M_OWNERSHIP=mode, X3M_CAMERA='vanilla',
                       X3M_MOTION_OUTPUT='1', X3M_SCENE_HOOK='0', X3M_TELEMETRY='0',
                       X3M_HDR='1', X3M_HDR_TONEMAP='agx', X3M_TAA='1', X3M_MOTION_JITTER='1',
                       X3M_HDR_EXPOSURE='manual', X3M_HDR_EV_MANUAL='0', X3M_HDR_BLOOM='0',
                       WINEDLLOVERRIDES='d3d9=n,b')
            command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=n,b',
                       '--workdir', str(run_dir), str(run_dir / exe.name)]
            started = time.monotonic()
            completed = subprocess.run(command, env=env, capture_output=True, text=True, timeout=120)
            elapsed = time.monotonic() - started
            stdout = results / f'capture-bloom-x3-ownership-{mode}.txt'
            stderr = results / f'capture-bloom-x3-ownership-{mode}-wine.log'
            stdout.write_text(completed.stdout); stderr.write_text(completed.stderr)
            item = {'exit': completed.returncode, 'elapsed_seconds': elapsed,
                    'stdout_path': str(stdout), 'stderr_path': str(stderr),
                    'run_directory': str(run_dir), 'environment': {k: v for k, v in env.items() if k.startswith('X3M_')}}
            report['modes'][mode] = item
            save()
            if completed.returncode != 0:
                raise RuntimeError(f'ownership={mode}: fixture exit {completed.returncode}; see {stdout}')
            item.update(parse_output(completed.stdout))
            save()
            print(f'ownership={mode}: {item["checks"]} checks, {item["skipped"]} skips, {elapsed:.2f}s', flush=True)
        if sha(args.dll) != report['dll_sha256']:
            raise RuntimeError('Selected DLL changed during run')
        report.update(status='PASS', passed=True)
    except BaseException as error:
        report.update(status='FAIL', error=repr(error))
        raise
    finally:
        save()
        print(summary, flush=True)

if __name__ == '__main__':
    main()
