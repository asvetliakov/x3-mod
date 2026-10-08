#!/usr/bin/env python3
"""Engine heat shimmer fixture (docs/architecture/engine-exhaust-gap-analysis.md gap 9, phase 5).

Builds verification/probe/engine_shimmer_fixture.cpp with the production EngineShimmerPass and the rect builder
(src/proxy/engine_shimmer_core.h over engine_plumes_core.h), refusing a stale embedded program
(engine-shimmer-pixel-program.json must match its source and header), runs it once in the selected bottle with builtin
D3D9 and writes <results>/engine-shimmer/summary.json: bottle, the checkout's commit and the production sources'
hashes, the build, every CHECK and the numbers of each case (displacement against the amplitude and the replica's
mask, byte-equality outside the rects and after the revert, the fade, the 24 px gate and the cap, occlusion, hostile
state, refusals, faults, Reset, and the EVENT-fenced cost of run + revert for 1 / 4 / 16 rects of 10 % of the screen at
1920x1080 and 5120x1440). The fixture's stdout stays under verification/probe/build/engine-shimmer/. Run through
wine_lock.py with X3M_FIXTURE_BOTTLE=X3; --wine-env NAME=VALUE (repeatable, e.g. CX_GRAPHICS_BACKEND=wined3d) goes to
the wine wrapper as --env and into the record as wine_env. Never launches the game.
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
BUILD = PROBE / 'build/engine-shimmer'
EXE = BUILD / 'engine_shimmer_fixture.exe'
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-msse2', '-mfpmath=sse',
         '-mstackrealign', '-mincoming-stack-boundary=2', '-static', '-static-libgcc', '-static-libstdc++',
         '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-DX3M_ENGINE_SHIMMER_FIXTURE']
SOURCES = ('verification/probe/engine_shimmer_fixture.cpp', 'src/renderer/engine_shimmer_pass.cpp')
PRODUCTION_SOURCES = ('src/proxy/engine_shimmer_core.h', 'src/proxy/engine_plumes_core.h', 'src/proxy/engine_effects_core.h',
                      'src/renderer/engine_shimmer_pass.h', 'src/renderer/engine_shimmer_pass.cpp',
                      'src/effects/engine_shimmer_ps.hlsl', 'src/renderer/engine_shimmer_pixel_program_inc.h',
                      'src/renderer/quad_vertex_program.h', 'src/renderer/quad_vertex_program_inc.h')
PROGRAM = ('verification/results/engine-shimmer-pixel-program.json', 'src/renderer/engine_shimmer_pixel_program_inc.h')
TAGS = ('ATTACH', 'SIZE', 'DISPLACE', 'GATE', 'OCCLUSION', 'STATE', 'SCRATCH', 'REFUSAL', 'FAULT', 'RESET', 'TIMING')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_binding():
    def git(*args):
        return subprocess.run(['git', '-C', str(ROOT), *args], capture_output=True, text=True, check=True).stdout
    changed = [line[3:] for line in git('status', '--porcelain', '--', *PRODUCTION_SOURCES).splitlines()]
    commit = git('rev-parse', 'HEAD').strip() + ('-dirty' if git('status', '--porcelain', '--untracked-files=no').strip() else '')
    return {'commit': commit, 'production_sources_dirty': bool(changed), 'dirty_paths': changed,
            'sha256': {path: sha(ROOT / path) for path in PRODUCTION_SOURCES}}


def program_current():
    record = json.loads((ROOT / PROGRAM[0]).read_text())
    if record['source_sha256'] != sha(ROOT / record['source']) or record['header_sha256'] != sha(ROOT / PROGRAM[1]):
        raise SystemExit('engine_shimmer_ps: stale embedded program; run tools/shaders/generate_engine_shimmer_program.py')
    return {k: record[k] for k in ('source', 'word_count', 'bytecode_sha256', 'target')}


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
    report = {tag.lower(): [] for tag in TAGS}
    report.update(checks=[], result=None)
    for line in text.splitlines():
        head = line.split(' ', 1)[0]
        if head == 'CHECK':
            _, label, verdict = line.split(' ', 2)
            report['checks'].append([label, verdict.strip() == 'PASS'])
        elif head in TAGS:
            report[head.lower()].append(fields(line))
        elif head == 'RESULT':
            report['result'] = dict(fields(line), verdict=line.split()[1])
    report['check_count'] = len(report['checks'])
    report['failed_checks'] = [label for label, passed in report['checks'] if not passed]
    return report


def summary(r):
    return {
        'timing_gpu_ms': {f"{x['width']}x{x['height']}_{x['rects']}": x['gpu_ms'] for x in r['timing']},
        'timing_submit_ms': {f"{x['width']}x{x['height']}_{x['rects']}": x['submit_ms'] for x in r['timing']},
        'displace': {f"{x['width']}x{x['height']}": {k: x[k] for k in ('amplitude_px', 'max_px', 'max_over_mask_px',
                                                                     'mean_strong_px', 'near_px', 'mid_px', 'far_px',
                                                                     'side_px', 'outside_diff', 'band_diff', 'restored')}
                     for x in r['displace']},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--timeout', type=int, default=1800)
    parser.add_argument('--only', default=None, help='fixture case filter (comma list), diagnosis only: the record is not passed')
    parser.add_argument('--wine-env', action='append', default=[], metavar='NAME=VALUE',
                        help="Pass a variable through CrossOver's `wine --env` (applied after the bottle's environment), e.g. "
                             'CX_GRAPHICS_BACKEND=wined3d or =dxvk to select the builtin d3d9 for one run')
    args = parser.parse_args()
    for item in args.wine_env:
        if '=' not in item or not item.split('=', 1)[0]:
            parser.error(f'--wine-env expects NAME=VALUE: {item}')
    if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3':
        raise SystemExit('fixture requires X3M_FIXTURE_BOTTLE=X3')
    if game_running():
        raise SystemExit('the game is running')
    results = bottle.results_dir(ROOT) / 'engine-shimmer'
    results.mkdir(parents=True, exist_ok=True)
    record = {'bottle': bottle.describe(), 'production_sources': list(PRODUCTION_SOURCES), 'source': source_binding(),
              'program': program_current(), 'game_launched': False, 'wine_env': list(args.wine_env)}
    if not args.no_build:
        record['build'] = build()
    command = [bottle.WINE, *bottle.wine_args(), *[a for item in args.wine_env for a in ('--env', item)], '--dll', 'd3d9=b', '--workdir', str(BUILD), str(EXE)]
    if args.only:
        command += ['--only', args.only]
    done = subprocess.run(command, capture_output=True, env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'), timeout=args.timeout)
    text = done.stdout.decode('utf-8', 'replace').replace('\r\n', '\n')
    (BUILD / 'fixture.log').write_text(text)
    (BUILD / 'fixture.stderr.txt').write_bytes(done.stderr)
    report = parse(text)
    record['run'] = {'exit_code': done.returncode, 'result': report['result'], 'log': str((BUILD / 'fixture.log').relative_to(ROOT))}
    record['report'] = report
    record['summary'] = summary(report)
    record['passed'] = (done.returncode == 0 and not report['failed_checks'] and report['result'] is not None and
                        report['result']['verdict'] == 'PASS' and not args.only and len(report['timing']) == 6 and
                        len(report['displace']) == 2 and record.get('build', {}).get('warnings', 0) == 0)
    if not args.only:
        (results / 'summary.json').write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps({'passed': record['passed'], 'checks': report['check_count'], 'failed': report['failed_checks'],
                      **record['summary'], 'results': str(results.relative_to(ROOT))}, indent=1))
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
