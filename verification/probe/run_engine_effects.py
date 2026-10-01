#!/usr/bin/env python3
"""Engine effects phase 1a fixture (X3M_ENGINE_EFFECTS; docs/architecture/engine-effects-modern.md sections 1, 2, 5).

Builds the proxy (the worktree's CMake build, incremental), the motion seam DLL (build_motion_output.sh) and
engine_effects_fixture.exe, then runs the fixture four times in the selected bottle through the seam DLL as d3d9.dll:
  main        off + --debug: scenario frames (a suppressed and a not_jet fixed-function draw with a pixel check, three
              effect-pair JET draws at z 0.25 / 1.125 / 2.0, an opaque JET draw, an unscoped pair draw, an opaque
              non-candidate draw), a ring-overflow frame, a Reset, frames past the census's eight, 305 candidate-free frames (engine_frame
              rows at frames 0 and 300 only)
  native      native + --debug: nothing suppressed or recorded, the census counts forwarded_native
  unverified  off without the identity seam: refused, every draw forwarded
  unpatched   off + --debug with the identity but without the call redirects (engine_effects_patch not installed):
              every glow-jet draw forwarded as forwarded_patch_missing, nothing recorded
  timing      off without the census: per-draw microseconds of the suppressed, not_jet and non-candidate paths
  plumes      plumes + preset strong + --debug on a device without --hdr --taa: suppressed as off (records, pixels),
              the plume stage refuses to arm with one engine_plumes_state row (reason hdr_taa_path, glow suppressed),
              engine_stage rows at the engine_frame cadence (armed=0, nothing drawn, preset strong)
Each run's directory holds x3m/engine_bodies.json, written here by tools/effects/engine_bodies.py's dumps() for the
synthetic body manager the fixture builds (default path: <EXE directory>\\x3m\\engine_bodies.json). The effects pair's
program bytes are local inputs (/tmp/x3-shader-sweep/programs, never in the repository). The fixture's CHECK lines and
the session log's engine_effects_* / engine_draw / engine_frame rows are validated here; the record goes to
<results>/engine-effects/summary.json. Run through wine_lock.py with X3M_FIXTURE_BOTTLE=X3. Never launches the game.
"""
import argparse
import collections
import datetime
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory
import fixture_log  # X3M_LOG_FILE and X3M_CONFIG=bare
import fixture_process  # per-run timeout and cleanup
from game_guard import game_running

ROOT = Path(__file__).resolve().parents[2]
PROBE = ROOT / 'verification/probe'
BUILD = PROBE / 'build'
EXE = BUILD / 'engine_effects_fixture.exe'
SEAM = BUILD / 'motion-output-seam/d3d9.dll'
PROGRAMS = {'vs': Path('/tmp/x3-shader-sweep/programs/vs_d5e1c75351ed3f04.bin'),
            'ps': Path('/tmp/x3-shader-sweep/programs/ps_8360f422de08b5bd.bin')}
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-msse2', '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2']
MODES = {'main': dict(X3M_ENGINE_EFFECTS='off', X3M_DEBUG='1'), 'native': dict(X3M_ENGINE_EFFECTS='native', X3M_DEBUG='1'),
         'unverified': dict(X3M_ENGINE_EFFECTS='off', X3M_DEBUG='1'), 'unpatched': dict(X3M_ENGINE_EFFECTS='off', X3M_DEBUG='1'),
         'timing': dict(X3M_ENGINE_EFFECTS='off'),
         'plumes': dict(X3M_ENGINE_EFFECTS='plumes', X3M_ENGINE_EFFECTS_PRESET='strong', X3M_DEBUG='1')}
SCENARIO_FRAMES = (1, 2, 3, 5, 6, 7, 8, 9, 10, 11)
OVERFLOW_FRAME, RING = 4, 1024
# The production sources the fixture exercises: their content hashes and the checkout's commit go into the record
# (test_engine_effects compares them with the tree).
PRODUCTION_SOURCES = ('src/proxy/engine_effects.cpp', 'src/proxy/engine_effects.h', 'src/proxy/engine_effects_core.h',
                      'src/proxy/engine_effects_option.h', 'src/proxy/motion_output_engine_effects_inc.h',
                      'src/proxy/motion_output_engine_plumes_inc.h', 'src/proxy/engine_plumes_core.h')
QUIET_ROW_FRAME = 300  # main: the second candidate-free engine_frame row (frame 0 is the first)
ROW_VERDICTS = collections.Counter(suppressed=4, forwarded_opaque=1, forwarded_unscoped=1)  # engine_draw rows per scenario frame


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_binding():
    """The checkout's commit (-dirty when tracked files differ from it), whether the exercised production sources
    differ, and their content hashes."""
    def git(*args):
        return subprocess.run(['git', '-C', str(ROOT), *args], capture_output=True, text=True, check=True).stdout
    changed = [line[3:] for line in git('status', '--porcelain', '--', *PRODUCTION_SOURCES).splitlines()]
    commit = git('rev-parse', 'HEAD').strip() + ('-dirty' if git('status', '--porcelain', '--untracked-files=no').strip() else '')
    return {'commit': commit, 'production_sources_dirty': bool(changed), 'dirty_paths': changed,
            'sha256': {path: sha(ROOT / path) for path in PRODUCTION_SOURCES}}


def fields(line):
    return dict(re.findall(r'(\w+)=(\S+)', line))


def table():
    """The synthetic engine_bodies.json (schema 1, the generator's own serialisation) matching the fixture's manager:
    the named body in dynamic slot 0 (id 20000), v\\00566 by its fixed id, one body the manager never registers."""
    spec = importlib.util.spec_from_file_location('engine_bodies', ROOT / 'tools/effects/engine_bodies.py')
    eb = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(eb)
    body = lambda **kw: dict(dict(id=None, lists=['jet'], value=6250, z_min=-1.0, z_max=0.0, z_extent='negative', half_width=[0.5, 0.5],
                                  cluster='red', tier='big', effect='engine.fx'), **kw)
    return eb.dumps(dict(schema=eb.SCHEMA, tool='tools/effects/engine_bodies.py', generated_from={'root': '<fixture>'},
                         rules=dict(clusters={k: eb.hexrgb(v) for k, v in eb.CLUSTERS.items()}), counts=dict(bodies=3, missing=0),
                         bodies={'effects\\engines\\fx_engine_test_red': body(),
                                 'effects\\engines\\never_loaded': body(cluster='cyan'),
                                 'v\\00566': body(id=566, lists=['jet', 'smalljet'], value=520, cluster='grey', tier='tiny')},
                         missing=[]))


def build():
    log = BUILD / 'engine-effects-build.log'
    BUILD.mkdir(parents=True, exist_ok=True)
    with log.open('w') as out:
        for command in (['cmake', '--build', 'build', '-j8'], ['sh', 'verification/probe/build_motion_output.sh'],
                        ['i686-w64-mingw32-g++', *FLAGS, '-static', '-static-libgcc', '-static-libstdc++',
                         'verification/probe/engine_effects_fixture.cpp', '-o', str(EXE), '-ldxguid', '-luser32']):
            subprocess.run(command, cwd=ROOT, check=True, stdout=out, stderr=subprocess.STDOUT)
    text = log.read_text(errors='replace')
    return {'log': str(log.relative_to(ROOT)), 'warnings': len(re.findall(r'\bwarning:', text))}


def run(mode, timeout):
    directory = BUILD / ('engine-effects-' + mode + '-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
    (directory / 'x3m').mkdir(parents=True)
    shutil.copy(EXE, directory)
    shutil.copy(SEAM, directory / 'd3d9.dll')
    (directory / 'x3m/engine_bodies.json').write_text(table())
    env = {k: v for k, v in os.environ.items() if not k.startswith('X3M_')}
    env.update(X3M_FIXTURE_BOTTLE=bottle.BOTTLE, X3M_MOTION_OUTPUT='1', **MODES[mode], **fixture_log.session_log_env(directory))
    command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=n,b', '--workdir', str(directory), str(directory / EXE.name),
               'Z:' + str(PROGRAMS['vs']), 'Z:' + str(PROGRAMS['ps']), mode]
    done = fixture_process.run(command, build_dir=directory, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    stdout = done.stdout.decode('utf-8', 'replace').replace('\r\n', '\n')
    logs = sorted((directory / 'x3-modern-captures').glob('session-*.log'))
    session = logs[-1].read_text(errors='replace') if logs else ''
    (directory / 'stdout.txt').write_text(stdout)
    (directory / 'stderr.txt').write_bytes(done.stderr)
    return dict(directory=directory, exit=done.returncode, stdout=stdout, session=session)


def rows(text, tag):
    return [fields(line) for line in text.splitlines() if re.search(r'(^|\s)' + tag + r'\s', line)]


def validate(mode, r):
    """Fixture checks plus the session-log census; returns (problems, summary)."""
    problems, out = [], {}
    lines = r['stdout'].splitlines()
    checks = [l.split() for l in lines if l.startswith('CHECK ')]
    failed = [c[1] for c in checks if c[2] != 'PASS']
    result = [l for l in lines if l.startswith('RESULT ')]
    out.update(exit=r['exit'], checks=len(checks), failed=failed, result=result[-1] if result else None)
    if r['exit'] != 0 or failed or not result or not result[-1].startswith('RESULT PASS'):
        problems.append(f'{mode}: exit={r["exit"]} failed={failed} result={result[-1] if result else None}')
    log = r['session']
    if rows(log, 'engine_effects_partial'):  # the route is on in every mode: never the route-off partial state
        problems.append(f'{mode}: engine_effects_partial row with the route on')
    modes = rows(log, 'engine_effects_mode')
    out['mode_row'] = modes[0] if modes else None
    expected_status = {'main': 'armed', 'native': 'native', 'unverified': 'executable_mismatch', 'unpatched': 'armed', 'timing': 'armed',
                       'plumes': 'armed'}[mode]
    if len(modes) != 1 or modes[0].get('status') != expected_status:
        problems.append(f'{mode}: engine_effects_mode {modes}')
    if mode == 'timing':
        out['timing'] = {l.split()[1]: float(fields(l)['median_us_per_draw']) for l in lines if l.startswith('TIMING ')}
        if rows(log, 'engine_draw') or rows(log, 'engine_frame'):
            problems.append('timing: census rows without --debug')
        if len(out['timing']) != 3:
            problems.append(f'timing: {out["timing"]}')
        return problems, out
    plume_rows = rows(log, 'engine_effects_plumes') + rows(log, 'engine_plumes_state') + rows(log, 'engine_stage')
    if mode != 'plumes' and plume_rows:
        problems.append(f'{mode}: plume rows outside plumes: {plume_rows[:2]}')
    if mode == 'plumes':
        # Suppressed as off; the stage refuses to arm by configuration (no --hdr --taa in the seam) in one row and
        # writes engine_stage at the engine_frame cadence; nothing drawn.
        plumes = rows(log, 'engine_effects_plumes')
        state = rows(log, 'engine_plumes_state')
        stage = {int(r['frame']): r for r in rows(log, 'engine_stage')}
        frames = {int(f['frame']): f for f in rows(log, 'engine_frame')}
        out.update(plumes_row=plumes[0] if plumes else None, state_rows=state, stage_frames=sorted(stage))
        if [(r.get('preset'), r.get('setting'), r.get('status'), r.get('stage')) for r in plumes] != [('strong', 'strong', 'ok', 'requested')]:
            problems.append(f'plumes: engine_effects_plumes {plumes}')
        if [(r.get('armed'), r.get('reason'), r.get('glow'), r.get('drawn')) for r in state] != [('0', 'hdr_taa_path', 'suppressed', 'none')]:
            problems.append(f'plumes: engine_plumes_state {state}')
        for frame in (1, 2):
            f, g = frames.get(frame, {}), stage.get(frame, {})
            if (f.get('mode'), f.get('records'), f.get('suppressed')) != ('plumes', '4', '4'):
                problems.append(f'plumes: engine_frame {frame} {f}')
            if (g.get('armed'), g.get('ran'), g.get('nozzles'), g.get('records'), g.get('preset')) != ('0', '0', '0', '4', 'strong'):
                problems.append(f'plumes: engine_stage {frame} {g}')
        if sorted(stage) != sorted(frames):
            problems.append(f'plumes: engine_stage frames {sorted(stage)} != engine_frame frames {sorted(frames)}')
        return problems, out
    if mode == 'unverified':
        if rows(log, 'engine_draw') or rows(log, 'engine_frame') or rows(log, 'engine_effects_device'):
            problems.append('unverified: hook rows despite the refusal')
        return problems, out
    bodies = rows(log, 'engine_effects_bodies')
    resolve = rows(log, 'engine_effects_resolve')
    out['bodies'] = {k: v for k, v in bodies[0].items() if k != 'path'} if bodies else None  # no host paths in the record
    out['resolve'] = resolve[-1] if resolve else None
    if len(bodies) != 1 or bodies[0].get('event') != 'loaded' or bodies[0].get('bodies') != '3' or bodies[0].get('named') != '2' or bodies[0].get('smalljet') != '1':
        problems.append(f'{mode}: engine_effects_bodies {bodies}')
    if not resolve or resolve[-1].get('resolved') != '2' or resolve[-1].get('mapped') != '2':
        problems.append(f'{mode}: engine_effects_resolve {resolve}')
    draws = rows(log, 'engine_draw')
    frames = {int(f['frame']): f for f in rows(log, 'engine_frame')}
    per_frame = collections.defaultdict(collections.Counter)
    for d in draws:
        per_frame[int(d['frame'])][d['verdict']] += 1
    out['engine_draw_rows'] = len(draws)
    out['engine_draw_frames'] = sorted(per_frame)
    out['engine_frame_rows'] = len(frames)
    if mode in ('native', 'unpatched'):
        verdict = 'forwarded_native' if mode == 'native' else 'forwarded_patch_missing'
        redirects = '1' if mode == 'native' else '0'  # native: the fixture's seam marks them live; nothing is suppressed anyway
        for frame in (1, 2):
            want = collections.Counter({verdict: 4, 'forwarded_opaque': 1, 'forwarded_unscoped': 1})
            if per_frame[frame] != want:
                problems.append(f'{mode}: frame {frame} rows {dict(per_frame[frame])}')
            f = frames.get(frame, {})
            if (f.get('records'), f.get('suppressed'), f.get(verdict), f.get('redirects')) != ('0', '0', '4', redirects):
                problems.append(f'{mode}: engine_frame {frame} {f}')
        return problems, out
    # main: rows in the first eight frames that have any (1..8), none after; 64 in the overflow frame.
    if sorted(per_frame) != list(range(1, 9)):
        problems.append(f'main: engine_draw frames {sorted(per_frame)}')
    for frame in range(1, 9):
        if frame == OVERFLOW_FRAME:
            if sum(per_frame[frame].values()) != 64 or set(per_frame[frame]) != {'suppressed'}:
                problems.append(f'main: overflow rows {dict(per_frame[frame])}')
        elif per_frame[frame] != ROW_VERDICTS:
            problems.append(f'main: frame {frame} rows {dict(per_frame[frame])}')
    # The pair draws' rows carry the selected order and the body names; the fixed-function JET draw has no geometry.
    first = [d for d in draws if d['frame'] == '1' and d['verdict'] == 'suppressed']
    out['frame1_rows'] = [{k: d[k] for k in ('model', 'name', 'z', 's', 'order', 'cluster', 'pair', 'za_basis', 'zb_basis')} for d in first]
    if [(d['model'], d['name'], d['order'], d['cluster']) for d in first] != [
            ('20000', 'effects\\engines\\fx_engine_test_red', 'invalid', 'red'), ('20000', 'effects\\engines\\fx_engine_test_red', 'a', 'red'),
            ('566', 'v\\00566', 'a', 'grey'), ('777', '-', 'b', 'white')]:
        problems.append(f'main: frame 1 rows {out["frame1_rows"]}')
    # The node basis cross-check: the matching order's unit z agrees with the node's z basis row (16.16).
    for d in first[1:]:
        cos = float(d['za_basis'] if d['order'] == 'a' else d['zb_basis'])
        if cos < 0.9999:
            problems.append(f'main: basis cross-check {d["model"]} {cos}')
    # engine_frame: every frame with a candidate (1..11); the candidate-free frames 0 and 12..316 at most once per 300
    # (frames 0 and 300).
    if sorted(frames) != [*range(0, 12), QUIET_ROW_FRAME] or any(frames[f].get('candidates') != '0' for f in (0, QUIET_ROW_FRAME)):
        problems.append(f'main: engine_frame rows {sorted(frames)}')
    if any('ring_overflow' in f for f in frames.values()):
        problems.append('main: engine_frame still carries ring_overflow=')
    for frame in SCENARIO_FRAMES:
        f = frames.get(frame, {})
        want = dict(candidates='7', not_jet='1', records='4', suppressed='4', forwarded_unscoped='1', forwarded_opaque='1', forwarded_overflow='0',
                    forwarded_patch_missing='0', redirects='1',
                    unknown_body='1', steering='1', rows_unknown='1', order_a='2', order_b='1', order_mismatch='0', order_invalid='1')
        if {k: f.get(k) for k in want} != want:
            problems.append(f'main: engine_frame {frame} {f}')
    o = frames.get(OVERFLOW_FRAME, {})
    if (o.get('records'), o.get('suppressed'), o.get('forwarded_overflow'), o.get('rows'), o.get('rows_more')) != (
            str(RING), str(RING), '6', '64', str(RING + 6 - 64)):
        problems.append(f'main: overflow engine_frame {o}')
    if len(rows(log, 'engine_effects_device')) != 1:
        problems.append('main: engine_effects_device rows')
    if 'RESET PASS' not in r['stdout']:
        problems.append('main: no Reset')
    out['records'] = [l for l in lines if l.startswith('RECORD ') and ' frame=1 ' in l]
    return problems, out


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--timeout', type=int, default=300)
    parser.add_argument('modes', nargs='*', default=list(MODES))
    args = parser.parse_args()
    if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3':
        raise SystemExit('fixture requires X3M_FIXTURE_BOTTLE=X3')
    if game_running():
        raise SystemExit('the game is running')
    for path in PROGRAMS.values():
        if not path.is_file():
            raise SystemExit(f'local program bytes missing: {path} (the shader sweep; skipping is not a pass)')
    results = bottle.results_dir(ROOT) / 'engine-effects'
    results.mkdir(parents=True, exist_ok=True)
    record = {'bottle': bottle.describe(), 'programs': {k: {'path': str(p), 'sha256': sha(p)} for k, p in PROGRAMS.items()},
              'production_sources': list(PRODUCTION_SOURCES), 'source': source_binding(), 'game_launched': False}
    if not args.no_build:
        record['build'] = build()
    record['binaries'] = {'fixture_sha256': sha(EXE), 'seam_sha256': sha(SEAM), 'dll_sha256': sha(ROOT / 'build/d3d9.dll')}
    problems, runs = [], {}
    for mode in args.modes:
        r = run(mode, args.timeout)
        p, summary = validate(mode, r)
        summary['directory'] = str(r['directory'].relative_to(ROOT))
        runs[mode] = summary
        problems += p
        print(f'{mode}: exit={r["exit"]} checks={summary["checks"]} failed={len(summary["failed"])} problems={len(p)}', flush=True)
    record.update(runs=runs, problems=problems, passed=not problems and set(args.modes) == set(MODES))
    (results / 'summary.json').write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps({'passed': record['passed'], 'problems': problems, 'timing': runs.get('timing', {}).get('timing'),
                      'checks': {m: s['checks'] for m, s in runs.items()}, 'results': str(results.relative_to(ROOT))}, indent=1))
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
