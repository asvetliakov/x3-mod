#!/usr/bin/env python3
"""Engine plumes fixture (X3M_ENGINE_EFFECTS=plumes, phase 2; docs/architecture/engine-effects-modern.md sections 3-6).

Builds verification/probe/engine_plumes_fixture.cpp with the production EnginePlumesPass, the record -> vertex builder
(src/proxy/engine_plumes_core.h) and the production TemporalPass (refusing stale embedded programs: each
engine-plume-*-program.json must match its source and header), runs it once in the selected bottle with builtin D3D9
and writes <results>/engine-plumes/summary.json: bottle, the checkout's commit and the production sources' hashes,
the build, every CHECK, and the numbers of each case (lengths, widths and the law against the CPU replica for
s = 0 / 0.5 / 1, core survival and trail through the real resolve at 0 / 4 / 8 px per frame over the dark and the
flickering sky, the occlusion cuts and rim widths (centred and off-centre at 90 % of the width), the chase cap and
fade, the presets, the temporal variation, the bulge and taper, the shock cells, after flight C the end-on energy at
0 / 30 / 60 / 90 degrees, the ship floor and the mouth against the body, Reset, the FP16 refusal, the EVENT-fenced stage
cost at 30 / 100 nozzles and the CPU build with and without the ship floor).
The fixture's stdout stays under verification/probe/build/engine-plumes/. Run through wine_lock.py with
X3M_FIXTURE_BOTTLE=X3. Never launches the game.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory
from game_guard import game_running

ROOT = Path(__file__).resolve().parents[2]
PROBE = ROOT / 'verification/probe'
BUILD = PROBE / 'build/engine-plumes'
EXE = BUILD / 'engine_plumes_fixture.exe'
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-msse2', '-mfpmath=sse',
         '-mstackrealign', '-mincoming-stack-boundary=2', '-static', '-static-libgcc', '-static-libstdc++',
         '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-DX3M_ENGINE_PLUMES_FIXTURE']
SOURCES = ('verification/probe/engine_plumes_fixture.cpp', 'src/renderer/engine_plumes_pass.cpp',
           'src/renderer/temporal_pass.cpp')
# The production sources the fixture exercises: their content hashes and the checkout's commit go into the record
# (test_engine_plumes compares them with the tree).
PRODUCTION_SOURCES = ('src/proxy/engine_plumes_core.h', 'src/proxy/engine_effects_core.h', 'src/renderer/fog_transmittance.h',
                      'src/renderer/engine_plumes_pass.h',
                      'src/renderer/engine_plumes_pass.cpp', 'src/effects/engine_plume_vs.hlsl', 'src/effects/engine_plume_ps.hlsl',
                      'src/renderer/engine_plume_vertex_program_inc.h', 'src/renderer/engine_plume_pixel_program_inc.h',
                      'src/renderer/temporal_pass.h', 'src/renderer/temporal_pass.cpp')
PROGRAMS = {'engine_plume_vs': ('verification/results/engine-plume-vertex-program.json', 'src/renderer/engine_plume_vertex_program_inc.h'),
            'engine_plume_ps': ('verification/results/engine-plume-pixel-program.json', 'src/renderer/engine_plume_pixel_program_inc.h')}
GATES = {'core_survival': 0.9, 'trail_dark_px': 3, 'stage_gpu_ms_advisory': 0.1, 'build_100_ms': 0.1}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_binding():
    """The checkout's commit (-dirty when tracked files differ from it), whether the exercised production sources
    differ from it, and their content hashes."""
    def git(*args):
        return subprocess.run(['git', '-C', str(ROOT), *args], capture_output=True, text=True, check=True).stdout
    changed = [line[3:] for line in git('status', '--porcelain', '--', *PRODUCTION_SOURCES).splitlines()]
    commit = git('rev-parse', 'HEAD').strip() + ('-dirty' if git('status', '--porcelain', '--untracked-files=no').strip() else '')
    return {'commit': commit, 'production_sources_dirty': bool(changed), 'dirty_paths': changed,
            'sha256': {path: sha(ROOT / path) for path in PRODUCTION_SOURCES}}


def programs_current():
    """Every embedded plume program matches its provenance record (source and header hashes)."""
    out = {}
    for name, (record_path, header) in PROGRAMS.items():
        record = json.loads((ROOT / record_path).read_text())
        if record['source_sha256'] != sha(ROOT / record['source']) or record['header_sha256'] != sha(ROOT / header):
            raise SystemExit(f'{name}: stale embedded program; run tools/shaders/generate_rigid_motion_pixel.py --shader {name}')
        out[name] = {k: record[k] for k in ('source', 'word_count', 'bytecode_sha256', 'target')}
    return out


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    command = ['i686-w64-mingw32-g++', *FLAGS, *SOURCES, '-o', str(EXE), '-luser32', '-ldxguid']
    done = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    (BUILD / 'build.log').write_text(done.stdout + done.stderr)
    if done.returncode:
        raise SystemExit('fixture build failed:\n' + done.stdout + done.stderr)
    return {'command': [str(c) for c in command], 'warnings': len(re.findall(r'\bwarning:', done.stdout + done.stderr)),
            'executable_sha256': sha(EXE)}


def fields(line):
    out = {}
    for key, value in re.findall(r'(\w+)=(\S+)', line):
        try:
            out[key] = int(value) if re.fullmatch(r'-?\d+', value) else float(value)
        except ValueError:
            out[key] = value
    return out


def parse(text):
    tags = ('ATTACH', 'FP16_REFUSED', 'RESOLVE_CONFIG', 'LENGTH', 'RESOLVE', 'OCCLUSION_HEADON', 'OCCLUSION_20DEG',
            'OCCLUSION_TAILON', 'OCCLUSION_OFFCENTRE', 'CHASE', 'PRESETS', 'TEMPORAL', 'SHAPE', 'SHOCK', 'END_ON', 'FLOOR', 'MOUTH',
            'OFF_PATH', 'FAULT', 'RESET', 'TIMING', 'BUILD', 'BUILD_SHIPS')
    report = {tag.lower(): [] for tag in tags}
    report.update(checks=[], result=None)
    for line in text.splitlines():
        head = line.split(' ', 1)[0]
        if head == 'CHECK':
            _, label, verdict = line.split(' ', 2)
            report['checks'].append([label, verdict.strip() == 'PASS'])
        elif head in tags:
            report[head.lower()].append(fields(line))
        elif head == 'RESULT':
            report['result'] = dict(fields(line), verdict=line.split()[1])
    report['check_count'] = len(report['checks'])
    report['failed_checks'] = [label for label, passed in report['checks'] if not passed]
    return report


def gates(r):
    out = {}
    out['core_survival_min'] = min((x['core_survival'] for x in r['resolve']), default=None)
    out['core_survival'] = bool(r['resolve']) and all(x['core_survival'] >= GATES['core_survival'] for x in r['resolve'])
    dark = [x for x in r['resolve'] if x['sky'] == 'dark']
    out['trail_dark_px_max'] = max((x['trail_px'] for x in dark), default=None)
    out['trail_dark'] = bool(dark) and all(x['trail_px'] <= GATES['trail_dark_px'] for x in dark)
    out['trail_flicker_px'] = {f"{x['width']}_{x['speed_px']:.0f}px": x['trail_px'] for x in r['resolve'] if x['sky'] == 'flicker'}
    out['stage_gpu_ms'] = {f"{x['width']}x{x['height']}_{x['nozzles']}": x['gpu_ms'] for x in r['timing']}
    out['stage_gpu_within_advisory'] = {k: v <= GATES['stage_gpu_ms_advisory'] for k, v in out['stage_gpu_ms'].items()}
    out['build_us'] = {str(x['records']): x['median_us'] for x in r['build']}
    out['build_ships_us'] = {str(x['records']): x['median_us'] for x in r['build_ships']}
    out['grouping_us'] = {str(x['records']): x['grouping_us'] for x in r['build_ships']}
    out['build_100_within'] = any(x['records'] == 100 and x['median_us'] <= 1000 * GATES['build_100_ms'] for x in r['build'])
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
    results = bottle.results_dir(ROOT) / 'engine-plumes'
    results.mkdir(parents=True, exist_ok=True)
    record = {'bottle': bottle.describe(), 'production_sources': list(PRODUCTION_SOURCES), 'source': source_binding(),
              'programs': programs_current(), 'gates': GATES, 'game_launched': False}
    if not args.no_build:
        record['build'] = build()
    d3dx = bottle.game_dir() / 'd3dx9_37.dll'
    record['d3dx9_37_sha256'] = sha(d3dx)
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
    record['passed'] = (done.returncode == 0 and not report['failed_checks'] and report['result'] is not None and
                        report['result']['verdict'] == 'PASS' and not args.only and record['gates_met']['core_survival'] and
                        record['gates_met']['trail_dark'] and record['gates_met']['build_100_within'] and
                        record.get('build', {}).get('warnings', 0) == 0)
    (results / 'summary.json').write_text(json.dumps(record, indent=2) + '\n')
    g = record['gates_met']
    print(json.dumps({'passed': record['passed'], 'checks': report['check_count'], 'failed': report['failed_checks'],
                      'core_survival_min': g['core_survival_min'], 'trail_dark_px_max': g['trail_dark_px_max'],
                      'trail_flicker_px': g['trail_flicker_px'], 'stage_gpu_ms': g['stage_gpu_ms'], 'build_us': g['build_us'],
                      'grouping_us': g['grouping_us'],
                      'results': str(results.relative_to(ROOT))}, indent=1))
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
