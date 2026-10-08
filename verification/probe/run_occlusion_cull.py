#!/usr/bin/env python3
"""Occlusion cull fixture: one Wine run of occlusion_cull_fixture.exe on one backend (docs/architecture/occlusion-cull.md).

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_occlusion_cull.py --backend wined3d|dxvk --exe <exe> [--name <record>]

wined3d loads the bottle's d3d9 by name (override d3d9=b, CX_GRAPHICS_BACKEND=wined3d); dxvk loads CrossOver's bundled
DXVK d3d9.dll by path (d3d9=b, CX_GRAPHICS_BACKEND=dxvk), as run_d3d9_backend_smoke.py does. The bottle configuration is
never edited. Writes verification/results/occlusion-cull-batched/<name>.json (default name fixture-<backend>; bottle,
command, provenance hashes, the parsed CHECK/FRAME/SUMMARY/REALFRAME/REALSUMMARY/COST/DERIVED rows) and <name>.txt
(stdout); never builds, never launches the game. The Run137 comparison driver (legacy_cost.exe, built by
verification/results/occlusion-cull-batched/legacy_build.sh) runs through it with --name legacy-<backend>.
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
RESULTS = ROOT / 'verification/results/occlusion-cull-batched'
DXVK = Path('/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/lib/dxvk/i386-windows/d3d9.dll')
SOURCES = ('verification/probe/occlusion_cull_fixture.cpp', 'verification/probe/run_occlusion_cull.py',
           'src/renderer/occlusion_cull_pass.cpp', 'src/renderer/occlusion_cull_pass.h', 'src/proxy/occlusion_cull_core.h',
           'verification/probe/occlusion_cull_scene_inc.h', 'verification/results/occlusion-cull-batched/legacy_cost.cpp')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def windows_path(path):
    return 'Z:' + str(path).replace('/', '\\')


def fields(text):
    out = {}
    for key, value in re.findall(r'(\w+)=(\S+)', text):
        out[key] = (int(value) if re.fullmatch(r'-?\d+', value)
                    else float(value) if re.fullmatch(r'-?\d+\.\d+', value) else value)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--backend', choices=('wined3d', 'dxvk'), required=True)
    parser.add_argument('--exe', type=Path, required=True, help='The CMake-built occlusion_cull_fixture.exe')
    parser.add_argument('--name', help='Record name (default fixture-<backend>)')
    parser.add_argument('--repeats', type=int, default=1, help='Cost phase repeats (interleaved, medians), 1..9')
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
               '--env', f'CX_GRAPHICS_BACKEND={args.backend}', str(exe), dll, str(max(1, min(9, args.repeats)))]
    RESULTS.mkdir(parents=True, exist_ok=True)
    name = args.name or f'fixture-{args.backend}'
    record = {'passed': False, 'name': name, 'game_launched': False, 'backend': args.backend, 'bottle': bottle.describe(),
              'sources': {s: sha(ROOT / s) for s in SOURCES}, 'executable_sha256': sha(exe),
              'd3d9_sha256': sha(DXVK) if args.backend == 'dxvk' else None, 'command': command}
    built_from = exe.parent / 'BUILT_FROM'
    if built_from.exists():  # legacy_cost.exe: built from another commit's pass and core (legacy_build.sh)
        record['built_from'] = built_from.read_text().strip()
        record['built_sources'] = {str(f.relative_to(exe.parent)): sha(f) for f in sorted((exe.parent / 'src').rglob('*'))
                                   if f.is_file()}
        record['sources_note'] = ('sources: the worktree files at run time (provenance of the runner, scene and driver); '
                                  'built_sources: the pass and core the executable was built from')
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
    record['real_frames'] = [fields(line[10:]) for line in lines if line.startswith('REALFRAME ')]
    record['real_summary'] = [fields(line[12:]) for line in lines if line.startswith('REALSUMMARY ')]
    record['cost'] = [fields(line[5:]) for line in lines if line.startswith('COST ')]
    record['derived'] = [fields(line[8:]) for line in lines if line.startswith('DERIVED ')]
    record['adapter'] = next((line[8:] for line in lines if line.startswith('ADAPTER ')), None)
    result = next((fields(line[7:]) for line in lines if line.startswith('RESULT ')), {})
    record['result'] = result
    record['failed_checks'] = [label for label, ok in record['checks'] if not ok]
    record['passed'] = record['exit_code'] == 0 and result.get('failed') == 0 and len(record['checks']) > 0
    assert record['sources'] == {s: sha(ROOT / s) for s in SOURCES}, 'Provenance changed during run'
    (RESULTS / f'{name}.txt').write_text(f'# {bottle.label()}\n# command: {" ".join(command)}\n' + stdout)
    (RESULTS / f'{name}.json').write_text(json.dumps(record, indent=1) + '\n')
    print(json.dumps({k: record[k] for k in ('name', 'backend', 'passed', 'exit_code', 'elapsed_s', 'adapter', 'result', 'failed_checks', 'summary', 'unready', 'real_summary', 'derived')}, indent=1))
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
