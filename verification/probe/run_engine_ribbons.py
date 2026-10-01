#!/usr/bin/env python3
"""Engine ribbons fixture (X3M_ENGINE_EFFECTS=plumes, phase 3; docs/architecture/engine-effects-modern.md sections 3-6).

Builds verification/probe/engine_ribbons_fixture.cpp with the production EngineRibbonsPass and EnginePlumesPass, the
ribbon pool and builder (src/proxy/engine_ribbons_core.h), the fog law (src/renderer/fog_transmittance.h) and the
production TemporalPass (refusing stale embedded programs: each engine-{plume,ribbon}-*-program.json must match its
source and header), runs it once in the selected bottle with builtin D3D9 and writes
<results>/engine-ribbons/summary.json: bottle, the checkout's commit and the production sources' hashes, the build,
every CHECK, and the numbers of each case (the moving nozzle at 4 / 8 px per frame over the dark and the flickering
sky through the real resolve: no gap at the nozzle, the near-nozzle survival, the trailing length against T v s; the
3 px floor; stationary; SETA; cut; identity loss; the 256 cap; fog; fault; Reset; the EVENT-fenced ribbon draw at 30 /
100 ribbons and the CPU update + build). The fixture's stdout stays under verification/probe/build/engine-ribbons/.
Run through wine_lock.py with X3M_FIXTURE_BOTTLE=X3. Never launches the game.
"""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory
from game_guard import game_running
import run_engine_plumes as plumes  # the shared helpers: sha, fields, git binding shape, the compile flags

ROOT = Path(__file__).resolve().parents[2]
PROBE = ROOT / 'verification/probe'
BUILD = PROBE / 'build/engine-ribbons'
EXE = BUILD / 'engine_ribbons_fixture.exe'
SOURCES = ('verification/probe/engine_ribbons_fixture.cpp', 'src/renderer/engine_ribbons_pass.cpp',
           'src/renderer/engine_plumes_pass.cpp', 'src/renderer/temporal_pass.cpp')
PRODUCTION_SOURCES = ('src/proxy/engine_ribbons_core.h', 'src/renderer/fog_transmittance.h', 'src/renderer/engine_ribbons_pass.h',
                      'src/renderer/engine_ribbons_pass.cpp', 'src/effects/engine_ribbon_vs.hlsl', 'src/effects/engine_ribbon_ps.hlsl',
                      'src/renderer/engine_ribbon_vertex_program_inc.h', 'src/renderer/engine_ribbon_pixel_program_inc.h',
                      'src/proxy/engine_plumes_core.h', 'src/proxy/engine_effects_core.h', 'src/renderer/engine_plumes_pass.h',
                      'src/renderer/engine_plumes_pass.cpp', 'src/renderer/temporal_pass.h', 'src/renderer/temporal_pass.cpp')
PROGRAMS = dict(plumes.PROGRAMS,
                engine_ribbon_vs=('verification/results/engine-ribbon-vertex-program.json', 'src/renderer/engine_ribbon_vertex_program_inc.h'),
                engine_ribbon_ps=('verification/results/engine-ribbon-pixel-program.json', 'src/renderer/engine_ribbon_pixel_program_inc.h'))
GATES = {'near_survival': 0.9, 'length_tolerance': 0.1, 'stage_gpu_ms_advisory': 0.1, 'cpu_100_ms': 0.1}
TAGS = ('ATTACH', 'FP16_REFUSED', 'RESOLVE_CONFIG', 'RIBBON_RESOLVE', 'RIBBON_FLOOR', 'RIBBON_STATIONARY', 'RIBBON_SETA',
        'RIBBON_CUT', 'RIBBON_LOSS', 'RIBBON_CAP', 'RIBBON_FOG', 'OFF_PATH', 'FAULT', 'RESET', 'TIMING', 'CPU')


def source_binding():
    def git(*args):
        return subprocess.run(['git', '-C', str(ROOT), *args], capture_output=True, text=True, check=True).stdout
    changed = [line[3:] for line in git('status', '--porcelain', '--', *PRODUCTION_SOURCES).splitlines()]
    commit = git('rev-parse', 'HEAD').strip() + ('-dirty' if git('status', '--porcelain', '--untracked-files=no').strip() else '')
    return {'commit': commit, 'production_sources_dirty': bool(changed), 'dirty_paths': changed,
            'sha256': {path: plumes.sha(ROOT / path) for path in PRODUCTION_SOURCES}}


def programs_current():
    out = {}
    for name, (record_path, header) in PROGRAMS.items():
        record = json.loads((ROOT / record_path).read_text())
        if record['source_sha256'] != plumes.sha(ROOT / record['source']) or record['header_sha256'] != plumes.sha(ROOT / header):
            raise SystemExit(f'{name}: stale embedded program; run tools/shaders/generate_rigid_motion_pixel.py --shader {name}')
        out[name] = {k: record[k] for k in ('source', 'word_count', 'bytecode_sha256', 'target')}
    return out


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    command = ['i686-w64-mingw32-g++', *plumes.FLAGS, *SOURCES, '-o', str(EXE), '-luser32', '-ldxguid']
    done = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    (BUILD / 'build.log').write_text(done.stdout + done.stderr)
    if done.returncode:
        raise SystemExit('fixture build failed:\n' + done.stdout + done.stderr)
    return {'command': [str(c) for c in command], 'warnings': (done.stdout + done.stderr).count('warning:'),
            'executable_sha256': plumes.sha(EXE)}


def parse(text):
    report = {tag.lower(): [] for tag in TAGS}
    report.update(checks=[], result=None)
    for line in text.splitlines():
        head = line.split(' ', 1)[0]
        if head == 'CHECK':
            _, label, verdict = line.split(' ', 2)
            report['checks'].append([label, verdict.strip() == 'PASS'])
        elif head in TAGS:
            report[head.lower()].append(plumes.fields(line))
        elif head == 'RESULT':
            report['result'] = dict(plumes.fields(line), verdict=line.split()[1])
    report['check_count'] = len(report['checks'])
    report['failed_checks'] = [label for label, passed in report['checks'] if not passed]
    return report


def gates(r):
    out = {}
    rows = r['ribbon_resolve']
    out['near_survival_min'] = min((x['survival'] for x in rows), default=None)
    out['near_survival'] = bool(rows) and all(x['survival'] >= GATES['near_survival'] for x in rows)
    out['length_ratio'] = {f"{x['width']}_{x['sky']}_{x['speed_px']:.0f}px": round(x['length_px'] / x['expected_px'], 4) for x in rows}
    out['length_within'] = bool(rows) and all(abs(x['length_px'] - x['expected_px']) <= GATES['length_tolerance'] * x['expected_px']
                                              for x in rows)
    out['stage_gpu_ms'] = {f"{x['width']}x{x['height']}_{x['ribbons']}": x['gpu_ms'] for x in r['timing']}
    out['stage_gpu_within_advisory'] = {k: v <= GATES['stage_gpu_ms_advisory'] for k, v in out['stage_gpu_ms'].items()}
    out['cpu_us'] = {str(x['ribbons']): x['median_us'] for x in r['cpu']}
    out['cpu_100_within'] = any(x['ribbons'] == 100 and x['median_us'] <= 1000 * GATES['cpu_100_ms'] for x in r['cpu'])
    out['fog'] = [{k: x[k] for k in ('expected_T', 'core_ratio', 'trail_ratio')} for x in r['ribbon_fog']]
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--timeout', type=int, default=1800)
    parser.add_argument('--only', default=None, help='fixture case filter (comma list), diagnosis only: the record is not passed')
    args = parser.parse_args()
    if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3':
        raise SystemExit('fixture requires X3M_FIXTURE_BOTTLE=X3')
    if game_running():
        raise SystemExit('the game is running')
    results = bottle.results_dir(ROOT) / 'engine-ribbons'
    results.mkdir(parents=True, exist_ok=True)
    record = {'bottle': bottle.describe(), 'production_sources': list(PRODUCTION_SOURCES), 'source': source_binding(),
              'programs': programs_current(), 'gates': GATES, 'game_launched': False}
    if not args.no_build:
        record['build'] = build()
    d3dx = bottle.game_dir() / 'd3dx9_37.dll'
    record['d3dx9_37_sha256'] = plumes.sha(d3dx)
    command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=b', '--workdir', str(BUILD), str(EXE), r'C:\X3\d3dx9_37.dll']
    if args.only:
        command += ['--only', args.only]
    env = dict(os.environ, WINEDLLOVERRIDES='d3d9=b')
    done = subprocess.run(command, capture_output=True, env=env, timeout=args.timeout)
    text = done.stdout.decode('utf-8', 'replace').replace('\r\n', '\n')
    (BUILD / 'fixture.log').write_text(text)
    (BUILD / 'fixture.stderr.txt').write_bytes(done.stderr)
    report = parse(text)
    record['run'] = {'exit_code': done.returncode, 'result': report['result'], 'log': str((BUILD / 'fixture.log').relative_to(ROOT))}
    record['report'] = report
    record['gates_met'] = gates(report)
    g = record['gates_met']
    record['passed'] = (done.returncode == 0 and not report['failed_checks'] and report['result'] is not None and
                        report['result']['verdict'] == 'PASS' and not args.only and g['near_survival'] and g['length_within'] and
                        g['cpu_100_within'] and record.get('build', {}).get('warnings', 0) == 0)
    (results / 'summary.json').write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps({'passed': record['passed'], 'checks': report['check_count'], 'failed': report['failed_checks'],
                      'near_survival_min': g['near_survival_min'], 'length_ratio': g['length_ratio'],
                      'stage_gpu_ms': g['stage_gpu_ms'], 'cpu_us': g['cpu_us'], 'fog': g['fog'],
                      'results': str(results.relative_to(ROOT))}, indent=1))
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
