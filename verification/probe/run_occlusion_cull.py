#!/usr/bin/env python3
"""Occlusion cull fixture: one Wine run of occlusion_cull_fixture.exe on one backend (docs/architecture/occlusion-cull.md).

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_occlusion_cull.py --backend wined3d|dxvk --exe <exe>

wined3d loads the bottle's d3d9 by name (override d3d9=b, CX_GRAPHICS_BACKEND=wined3d); dxvk loads CrossOver's bundled
DXVK d3d9.dll by path (d3d9=b, CX_GRAPHICS_BACKEND=dxvk), as run_d3d9_backend_smoke.py does. The bottle configuration is
never edited. Writes verification/results/occlusion-cull/fixture-<backend>.json (bottle, command, provenance hashes, the
parsed CHECK/FRAME/SUMMARY rows) and fixture-<backend>.txt (stdout); never builds, never launches the game.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE)

ROOT = Path(__file__).resolve().parents[2]
RESULTS = ROOT / 'verification/results/occlusion-cull'
DXVK = Path('/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/lib/dxvk/i386-windows/d3d9.dll')
SOURCES = ('verification/probe/occlusion_cull_fixture.cpp', 'verification/probe/run_occlusion_cull.py',
           'src/renderer/occlusion_cull_pass.cpp', 'src/renderer/occlusion_cull_pass.h', 'src/proxy/occlusion_cull_core.h')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def windows_path(path):
    return 'Z:' + str(path).replace('/', '\\')


def fields(text):
    out = {}
    for key, value in re.findall(r'(\w+)=(\S+)', text):
        out[key] = int(value) if re.fullmatch(r'-?\d+', value) else value
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--backend', choices=('wined3d', 'dxvk'), required=True)
    parser.add_argument('--exe', type=Path, required=True, help='The CMake-built occlusion_cull_fixture.exe')
    args = parser.parse_args()
    if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3':
        sys.exit('set X3M_FIXTURE_BOTTLE=X3 and run through verification/probe/wine_lock.py')
    exe = args.exe.resolve()
    if not exe.is_file():
        sys.exit(f'fixture not built: {exe}')
    if (exe.parent / 'd3d9.dll').exists():
        sys.exit('a d3d9.dll beside the fixture would be loaded first; build the fixture in a directory without one')
    dll = 'builtin' if args.backend == 'wined3d' else windows_path(DXVK)
    environment = dict(os.environ, WINEDLLOVERRIDES='d3d9=b', DXVK_LOG_LEVEL='none', MVK_CONFIG_LOG_LEVEL='1')
    command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=b', '--workdir', str(exe.parent),
               '--env', f'CX_GRAPHICS_BACKEND={args.backend}', str(exe), dll]
    RESULTS.mkdir(parents=True, exist_ok=True)
    record = {'passed': False, 'game_launched': False, 'backend': args.backend, 'bottle': bottle.describe(),
              'sources': {s: sha(ROOT / s) for s in SOURCES}, 'executable_sha256': sha(exe),
              'd3d9_sha256': sha(DXVK) if args.backend == 'dxvk' else None, 'command': command}
    started = time.time()
    try:
        run = subprocess.run(command, capture_output=True, text=True, env=environment, cwd=str(exe.parent), timeout=600)
        stdout, record['exit_code'] = run.stdout, run.returncode
    except subprocess.TimeoutExpired as e:
        stdout, record['exit_code'] = (e.stdout or b'').decode(errors='replace') if isinstance(e.stdout, bytes) else (e.stdout or ''), 'timeout'
        subprocess.run(['pkill', '-f', exe.name], check=False)
    record['elapsed_s'] = round(time.time() - started, 1)
    lines = stdout.splitlines()
    record['checks'] = [[line[6:].rsplit(' ', 1)[0], line.endswith('PASS')] for line in lines if line.startswith('CHECK ')]
    record['frames'] = [fields(line[6:]) for line in lines if line.startswith('FRAME ')]
    record['summary'] = [fields(line[8:]) for line in lines if line.startswith('SUMMARY ')]
    record['attach'] = [fields(line[7:]) for line in lines if line.startswith('ATTACH ')]
    record['unready'] = [fields(line[8:]) for line in lines if line.startswith('UNREADY ')]
    record['adapter'] = next((line[8:] for line in lines if line.startswith('ADAPTER ')), None)
    result = next((fields(line[7:]) for line in lines if line.startswith('RESULT ')), {})
    record['result'] = result
    record['failed_checks'] = [label for label, ok in record['checks'] if not ok]
    record['passed'] = record['exit_code'] == 0 and result.get('failed') == 0 and len(record['checks']) > 0
    assert record['sources'] == {s: sha(ROOT / s) for s in SOURCES}, 'Provenance changed during run'
    (RESULTS / f'fixture-{args.backend}.txt').write_text(f'# {bottle.label()}\n# command: {" ".join(command)}\n' + stdout)
    (RESULTS / f'fixture-{args.backend}.json').write_text(json.dumps(record, indent=1) + '\n')
    print(json.dumps({k: record[k] for k in ('backend', 'passed', 'exit_code', 'elapsed_s', 'adapter', 'result', 'failed_checks', 'summary', 'unready')}, indent=1))
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
