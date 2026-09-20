#!/usr/bin/env python3
"""Owner-only focused seam execution under wine_lock.py; never builds or installs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

import bottle

INPUTS = {
    'vs_53a0a641107ed76c.bin': 'bc402d1c2bfbbcb9fedd98890db845dab2a24da8cfb5a88a74c4eafa40f7a50c',
    'ps_8759c7838bbc86c2.bin': '9fd15484fe419295cfb3534bd4f978efc8855c1e3e6a06e776533497dad48dc0',
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def check(stdout, lazy):
    lines = stdout.splitlines()
    require(any(line.startswith('RESULT PASS ') for line in lines), 'fixture did not complete successfully')
    require(not any(' FAIL' in line for line in lines), 'fixture assertion failed')
    def rows(prefix):
        return [dict(field.split('=', 1) for field in line.split()[1:])
                for line in lines if line.startswith(prefix + ' ')]
    matrix = rows('LATTICE_GUARD')
    require(len(matrix) == 12, 'matrix count')
    require({(int(r['cycle']), int(r['mode']), int(r['dropped'])) for r in matrix} ==
            {(c, m, d) for c in range(2) for m in range(3) for d in range(2)}, 'matrix coverage')
    for row in matrix:
        mode, dropped = int(row['mode']), int(row['dropped'])
        broken = lazy and mode == 1 and dropped == 1
        require(int(row['lazy']) == int(lazy) and int(row['broken']) == int(broken), 'wrong route witness')
        require(row['sample_releases'] == '0' and row['native_calls'] == '1', 'sampling/dispatch violation')
        require(float(row['depth']) == (-1.0 if broken else .5), 'auxiliary write witness')
        if mode and dropped:
            require(int(row['query_releases']) > 0, 'missing actual resource callback')
        if mode == 2:
            require(row['query_restores'] == '0', 'guard allowed query restoration')
        if mode == 1 and dropped:
            require(int(row['query_restores']) > 0, 'old control did not exercise production restoration')
    nested = rows('LATTICE_GUARD_NESTED')
    require(len(nested) == 8 and {(int(r['cycle']), int(r['action'])) for r in nested} ==
            {(c, a) for c in range(2) for a in (1, 2, 4, 5)}, 'nested/failure coverage')
    require(all(r['result'] == '8876086c' for r in nested if r['action'] in ('1', '2')), 'nested API not refused')
    final = rows('LATTICE_GUARD_FINAL')
    require(len(final) == 2 and {r['routed'] for r in final} == {'0', '1'} and
            all(r['retired'] == r['pins'] == '1' for r in final), 'last application reference lifecycle')
    return dict(status='pass', matrix=matrix, query_refusal_and_failures=nested,
                final_app_release=final, selector_bypassed=True,
                historical_run193_causality_proved=False, native_windows_runtime_verified=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--dll', type=Path, required=True)
    parser.add_argument('--inputs', type=Path, default=Path('/tmp/x3-shader-sweep/programs'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    require(os.environ.get('X3M_FIXTURE_BOTTLE') == 'X3', 'explicit X3 bottle required')
    info = bottle.describe()
    require(info['wine_arch'] == 'arm64' and info['environment'].get('FEX_X87REDUCEDPRECISION') == '1' and
            info['environment'].get('WINEMSYNC') == '1', 'X3 environment mismatch')
    require((bottle.bottle_dir() / 'dosdevices/z:').resolve() == Path('/'), 'Z: must map host root')
    for name, expected in INPUTS.items():
        require(sha(args.inputs / name) == expected, 'reviewed shader input mismatch')
    args.output.mkdir(parents=True, exist_ok=False)
    summary = dict(status='running', bottle=info, game_launched=False, cases={},
                   exe_sha256=sha(args.exe), dll_sha256=sha(args.dll), input_sha256=INPUTS)
    started = time.monotonic()
    try:
        for route in ('perdraw', 'lazy'):
            directory = (args.output / route).resolve()
            directory.mkdir()
            shutil.copyfile(args.exe, directory / 'motion_output_fixture.exe')
            shutil.copyfile(args.dll, directory / 'd3d9.dll')
            for name in INPUTS:
                shutil.copyfile(args.inputs / name, directory / name)
            env = {k: v for k, v in os.environ.items() if not k.startswith('X3M_')}
            env.update(X3M_FIXTURE_BOTTLE='X3', X3M_MOTION_OUTPUT='1', X3M_MOTION_RT_MODE=route,
                       X3M_TAA='0', X3M_HDR='0', X3M_MOTION_JITTER='0', X3M_LATTICE_STATE='0',
                       X3M_CAPTURE_START='999999', X3M_CAPTURE_FRAMES='1', X3M_SCENE_HOOK='0',
                       X3M_MOTION_FRAME_LOG='1', X3M_OWNERSHIP='0', X3M_STATE_SHADOW='1',
                       WINEDLLOVERRIDES='d3d9=n,b')
            command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=n,b', '--workdir', str(directory),
                       str(directory / 'motion_output_fixture.exe'),
                       *['Z:' + str(directory / name).replace('/', '\\') for name in INPUTS], 'latticeguard']
            begin = time.monotonic()
            with (directory / 'stdout.txt').open('x') as stdout, (directory / 'wine.log').open('x') as stderr:
                completed = subprocess.run(command, env=env, stdout=stdout, stderr=stderr, timeout=180)
            case = dict(exit_code=completed.returncode, seconds=time.monotonic() - begin, command=command)
            summary['cases'][route] = case
            require(completed.returncode == 0, f'{route} fixture failed')
            require(sha(directory / 'd3d9.dll') == summary['dll_sha256'] and
                    sha(directory / 'motion_output_fixture.exe') == summary['exe_sha256'], 'frozen executable changed')
            case.update(check((directory / 'stdout.txt').read_text(), route == 'lazy'))
        require(sha(args.dll) == summary['dll_sha256'] and sha(args.exe) == summary['exe_sha256'], 'source executable changed')
        summary['status'] = 'pass'
    except BaseException as error:
        summary.update(status='fail', error=repr(error))
        raise
    finally:
        summary['seconds'] = time.monotonic() - started
        (args.output / 'summary.json').write_text(json.dumps(summary, indent=2, allow_nan=False) + '\n')
        print(json.dumps(dict(status=summary['status'], seconds=summary['seconds'])))


if __name__ == '__main__':
    main()
