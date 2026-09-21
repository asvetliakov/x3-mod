#!/usr/bin/env python3
"""Build (host only) and run the two partial-sun-occlusion fixtures in the X3 bottle; one compact record.

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_sun_occlusion.py
The hook fixture is a plain CPU executable; the GPU fixture runs with builtin D3D9. --no-build runs
existing binaries; --hook-only / --gpu-only select one. Never launches the game.
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
BUILD = ROOT / 'build/verification/sun-occlusion'
HOOK, GPU = BUILD / 'sun_occlusion_hook_fixture.exe', BUILD / 'sun_occlusion_fixture.exe'
SOURCES = ('src/proxy/engine_memory.cpp', 'src/proxy/sun_occlusion.cpp', 'src/proxy/sun_occlusion.h', 'src/proxy/sun_occlusion_core.h', 'src/proxy/engine_patch.cpp', 'src/proxy/cpu_state.h',
           'src/renderer/sun_occlusion_pass.cpp', 'src/renderer/sun_occlusion_pass.h', 'src/renderer/lens_visibility_variant.cpp', 'src/renderer/lens_visibility_variant.h',
           'src/renderer/sun_visibility_program_inc.h', 'src/renderer/sun_visibility_taps_inc.h', 'src/temporal/sun_visibility_ps.hlsl',
           'verification/probe/sun_occlusion_hook_fixture.cpp', 'verification/probe/sun_occlusion_fixture.cpp', 'verification/probe/build_sun_occlusion.py',
           'verification/probe/run_sun_occlusion.py')
EXPECTED_HOOK_CHECKS = 64   # a run that skips a section is not a pass
EXPECTED_GPU_CHECKS = 89  # with both optional RT2 formats available; each SKIPPED format line takes one off
SCENES = ('open', 'covered', 'half', 'three_quarter', 'quarter', 'screen_edge_open', 'screen_edge_covered')


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
    total = re.search(r'SUN OCCLUSION HOOK checks=(\d+) failures=(\d+)', text)
    counters = next((fields(l) for l in text.splitlines() if l.startswith('COUNTERS ')), None)
    return {'checks': int(total.group(1)) if total else None, 'failures': int(total.group(2)) if total else None, 'counters': counters,
            'failure_lines': [l for l in text.splitlines() if l.startswith('FAIL')][:40]}


def parse_gpu(text):
    lines = text.splitlines()
    checks = [l.split(' ', 2)[1:] for l in lines if l.startswith('CHECK ')]
    result = next((l for l in lines if l.startswith('RESULT ')), '')
    return {'checks': len(checks), 'failed_checks': [label for label, verdict in checks if verdict.strip() != 'PASS'],
            'result': dict(fields(result), verdict=result.split()[1] if result else None),
            'attach': next((fields(l) for l in lines if l.startswith('ATTACH ')), None),
            'scenes': {l.split()[1]: fields(l) for l in lines if l.startswith('SCENE ')},
            'steps': [fields(l) for l in lines if l.startswith('STEP ')], 'rise': next((fields(l) for l in lines if l.startswith('RISE ')), None),
            'lens_fraction': next((fields(l).get('fraction') for l in lines if l.startswith('LENS fraction=')), None),
            'lens_draws': [l.split()[1] for l in lines if l.startswith('LENS_DRAW ')], 'skipped': [l for l in lines if l.startswith('SKIPPED ')],
            'device_calls': next((fields(l) for l in lines if l.startswith('CALLS ')), None)}


def accept_hook(record):
    return record['exit_status'] == 0 and record['failures'] == 0 and record['checks'] == EXPECTED_HOOK_CHECKS


def accept_gpu(record):
    skipped_formats = len(record['skipped'])
    return (record['exit_status'] == 0 and record['result'].get('verdict') == 'PASS' and not record['failed_checks'] and record['checks'] == EXPECTED_GPU_CHECKS - skipped_formats
            and record['result'].get('checks') == record['checks'] and set(record['scenes']) == set(SCENES) and len(record['steps']) == 8 and len(record['lens_draws']) == 10
            and record['attach'] and 0 < record['attach']['slots'] <= 512)


def main():
    name = os.environ.get('X3M_FIXTURE_BOTTLE')
    if name != 'X3':
        sys.exit('set X3M_FIXTURE_BOTTLE=X3')
    args = sys.argv[1:]
    build = None
    if '--no-build' not in args:
        import build_sun_occlusion
        build = build_sun_occlusion.build()
    record = {'passed': False, 'game_launched': False, 'bottle': bottle.describe(name), 'sources': {s: sha(ROOT / s) for s in SOURCES},
              'module_audit': build['module_audit'] if build else None}
    results = bottle.results_dir(ROOT)
    ok = True
    if '--gpu-only' not in args:
        started = time.time()
        run = subprocess.run([bottle.WINE, *bottle.wine_args(name), str(HOOK)], capture_output=True, text=True, timeout=600)
        record['hook'] = dict(parse_hook(run.stdout), exit_status=run.returncode, elapsed_s=round(time.time() - started, 1), executable_sha256=sha(HOOK))
        (results / 'sun-occlusion-hook.txt').write_text(run.stdout)
        ok = accept_hook(record['hook']) and ok
    if '--hook-only' not in args:
        started = time.time()
        run = subprocess.run([bottle.WINE, *bottle.wine_args(name), '--dll', 'd3d9=b', '--workdir', str(BUILD), str(GPU)], capture_output=True, text=True, timeout=900,
                             env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'))
        record['gpu'] = dict(parse_gpu(run.stdout), exit_status=run.returncode, elapsed_s=round(time.time() - started, 1), executable_sha256=sha(GPU))
        (results / 'sun-occlusion-gpu.txt').write_text(run.stdout)
        ok = accept_gpu(record['gpu']) and ok
    record['passed'] = bool(ok) and record['sources'] == {s: sha(ROOT / s) for s in SOURCES}
    (results / 'sun-occlusion.json').write_text(json.dumps(record, indent=1) + '\n')
    summary = {'passed': record['passed']}
    if 'hook' in record:
        summary['hook'] = {k: record['hook'][k] for k in ('checks', 'failures', 'exit_status', 'counters', 'failure_lines')}
    if 'gpu' in record:
        gpu = record['gpu']
        summary['gpu'] = {'checks': gpu['checks'], 'failed_checks': gpu['failed_checks'], 'exit_status': gpu['exit_status'], 'slots': gpu['attach'] and gpu['attach']['slots'],
                          'scenes': {k: (v.get('twin_open'), v.get('twin_valid'), v.get('raw'), v.get('used')) for k, v in gpu['scenes'].items()},
                          'steps': [(s['n'], s['smoothed'], s['expected']) for s in gpu['steps']], 'rise': gpu['rise'], 'lens_fraction': gpu['lens_fraction'],
                          'lens_draws': len(gpu['lens_draws']), 'skipped': gpu['skipped'], 'device_calls': gpu['device_calls']}
    print(json.dumps(summary, indent=1))
    sys.exit(0 if record['passed'] else 1)


if __name__ == '__main__':
    main()
