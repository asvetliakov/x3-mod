#!/usr/bin/env python3
"""Build (host only) and run the own sun hook's two fixtures in the X3 bottle; one compact record.

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_own_sun_hook.py
The hook fixture is a plain CPU executable (the probe's own-sun answer and forwarding, the lens bracket); the GPU
fixture runs the lens-flare gain's law with builtin D3D9. --no-build runs existing binaries; --hook-only / --gpu-only
select one. Never launches the game.
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
ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/verification/own-sun-hook'
HOOK, GPU = BUILD / 'own_sun_hook_fixture.exe', BUILD / 'lens_flare_gain_fixture.exe'
SOURCES = ('src/proxy/engine_memory.cpp', 'src/proxy/own_sun_hook.cpp', 'src/proxy/own_sun_hook.h', 'src/proxy/own_sun_hook_core.h',
           'src/proxy/own_sun_core.h', 'src/proxy/engine_patch.cpp', 'src/proxy/cpu_state.h', 'src/proxy/lens_flare_gain.h',
           'verification/probe/own_sun_hook_fixture.cpp', 'verification/probe/lens_flare_gain_fixture.cpp', 'verification/probe/build_own_sun_hook.py',
           'verification/probe/run_own_sun_hook.py')
EXPECTED_HOOK_CHECKS = 46  # a run that skips a section is not a pass
EXPECTED_GPU_CHECKS = 8    # law, admits, BLENDFACTOR cap, occlusion query, three gain cases, G = 0 not issued
LENS_GAIN_CASES = {'off', 'half', 'zero'}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fields(line):
    out = {}
    for key, value in re.findall(r'(\w+)=(\S+)', line):
        try:
            out[key] = int(value) if re.fullmatch(r'-?\d+', value) else float(value)
        except ValueError:
            out[key] = value
    return out


def parse_hook(text):
    total = re.search(r'OWN SUN HOOK checks=(\d+) failures=(\d+)', text)
    counters = next((fields(l) for l in text.splitlines() if l.startswith('COUNTERS ')), None)
    return {'checks': int(total.group(1)) if total else None, 'failures': int(total.group(2)) if total else None, 'counters': counters,
            'failure_lines': [l for l in text.splitlines() if l.startswith('FAIL')][:40]}


def parse_gpu(text):
    lines = text.splitlines()
    checks = [l.split(' ', 2)[1:] for l in lines if l.startswith('CHECK ')]
    result = next((l for l in lines if l.startswith('RESULT ')), '')
    return {'checks': len(checks), 'failed_checks': [label for label, verdict in checks if verdict.strip() != 'PASS'],
            'result': dict(fields(result), verdict=result.split()[1] if result else None),
            'lens_gain': {fields(l)['name']: fields(l) for l in lines if l.startswith('LENS_GAIN ')},
            'lens_gain_cost': next((fields(l) for l in lines if l.startswith('LENS_GAIN_COST ')), None)}


def accept_hook(record):
    return record['exit_status'] == 0 and record['failures'] == 0 and record['checks'] == EXPECTED_HOOK_CHECKS


def accept_gpu(record):
    return (record['exit_status'] == 0 and record['result'].get('verdict') == 'PASS' and not record['failed_checks'] and record['checks'] == EXPECTED_GPU_CHECKS
            and record['result'].get('checks') == record['checks']
            and set(record['lens_gain']) == LENS_GAIN_CASES and all(c.get('restored') == 1 for c in record['lens_gain'].values())
            # G = 0: the admitted draw is not issued (no draw, no rasterised sample); every G > 0 case issues one that passes samples
            and all((c.get('issued'), c.get('skipped')) == ((0, 1) if name == 'zero' else (1, 0)) and (c.get('samples') == 0 if name == 'zero' else c.get('samples', 0) > 0)
                    for name, c in record['lens_gain'].items()))


def main():
    name = os.environ.get('X3M_FIXTURE_BOTTLE')
    if name != 'X3':
        sys.exit('set X3M_FIXTURE_BOTTLE=X3')
    args = sys.argv[1:]
    build = None
    if '--no-build' not in args:
        import build_own_sun_hook
        build = build_own_sun_hook.build()
    record = {'passed': False, 'game_launched': False, 'bottle': bottle.describe(name), 'sources': {s: sha(ROOT / s) for s in SOURCES},
              'module_audit': build['module_audit'] if build else None}
    results = bottle.results_dir(ROOT)
    ok = True
    if '--gpu-only' not in args:
        started = time.time()
        run = subprocess.run([bottle.WINE, *bottle.wine_args(name), str(HOOK)], capture_output=True, text=True, timeout=600)
        record['hook'] = dict(parse_hook(run.stdout), exit_status=run.returncode, elapsed_s=round(time.time() - started, 1), executable_sha256=sha(HOOK))
        (results / 'own-sun-hook.txt').write_text(run.stdout)
        ok = accept_hook(record['hook']) and ok
    if '--hook-only' not in args:
        started = time.time()
        run = subprocess.run([bottle.WINE, *bottle.wine_args(name), '--dll', 'd3d9=b', '--workdir', str(BUILD), str(GPU)], capture_output=True, text=True, timeout=600,
                             stdin=subprocess.DEVNULL, env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'))
        record['gpu'] = dict(parse_gpu(run.stdout), exit_status=run.returncode, elapsed_s=round(time.time() - started, 1), executable_sha256=sha(GPU))
        (results / 'own-sun-hook-gpu.txt').write_text(run.stdout)
        ok = accept_gpu(record['gpu']) and ok
    record['passed'] = bool(ok) and record['sources'] == {s: sha(ROOT / s) for s in SOURCES}
    (results / 'own-sun-hook.json').write_text(json.dumps(record, indent=1) + '\n')
    summary = {'passed': record['passed']}
    if 'hook' in record:
        summary['hook'] = {k: record['hook'][k] for k in ('checks', 'failures', 'exit_status', 'counters', 'failure_lines')}
    if 'gpu' in record:
        gpu = record['gpu']
        summary['gpu'] = {'checks': gpu['checks'], 'failed_checks': gpu['failed_checks'], 'exit_status': gpu['exit_status'],
                          'lens_gain': {k: (v.get('r'), v.get('g'), v.get('b'), v.get('expected_r'), v.get('restored'), v.get('issued'), v.get('samples')) for k, v in gpu['lens_gain'].items()},
                          'lens_gain_cost': gpu['lens_gain_cost']}
    print(json.dumps(summary, indent=1))
    sys.exit(0 if record['passed'] else 1)


if __name__ == '__main__':
    main()
