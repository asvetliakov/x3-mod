#!/usr/bin/env python3
"""Engine effects phase 1a fixture (X3M_ENGINE_EFFECTS; docs/architecture/engine-effects-modern.md sections 1, 2, 5).

Builds the proxy (the worktree's CMake build, incremental), the motion seam DLL (build_motion_output.sh) and
engine_effects_fixture.exe, then runs the fixture once per mode in the selected bottle through the seam DLL as d3d9.dll:
  main        off + --debug: scenario frames (a suppressed and a not_jet fixed-function draw with a pixel check, three
              effect-pair JET draws at z 0.25 / 1.125 / 2.0, an opaque JET draw, an unscoped pair draw, an opaque
              non-candidate draw), a ring-overflow frame, a Reset, frames past the census's eight, 305 candidate-free frames (engine_frame
              rows at frames 0 and 300 only)
  native      native + --debug: nothing suppressed or recorded, the census counts forwarded_native
  unverified  off without the identity seam: refused, every draw forwarded
  unpatched   off + --debug with the identity but without the call redirects (engine_effects_patch not installed):
              every glow-jet draw forwarded as forwarded_patch_missing, nothing recorded
  timing      X3M_ENGINE_EFFECTS unset (the DLL default since Run 123 A: engine_effects_mode setting=- mode=plumes
              status=armed) without the census or --hdr --taa (suppressed as off, the plume floor's radius read on): per-draw
              microseconds of the suppressed (one parent, five cycling parents, two alternating, none), not_jet and
              non-candidate paths
  plumes      plumes + preset strong + --debug on a device without --hdr --taa: suppressed as off (records, pixels),
              the plume stage refuses to arm with one engine_plumes_state row (reason hdr_taa_path, glow suppressed),
              engine_stage rows at the engine_frame cadence (armed=0, nothing drawn, preset strong)
  armed       plumes + preset strong + --debug with --hdr --taa (X3M_HDR, X3M_TAA, X3M_MOTION_JITTER) and the fixture
              camera, frames in the scene-boundary pattern: the production stage arms, attaches inside the resolve and
              draws the scene view's two records (one of another camera and one from the background phase skipped); a
              forced draw fault disarms 64 frames with one engine_plumes_failed row, three consecutive ones refuse until
              Reset (final=1), Reset releases the pass and the next armed frame recreates it; taa_references delta 0; the
              disarmed and refused frames forward the four glow jets natively (engine_frame forwarded_stage_off=4, 259
              frames); the SETA site unreadable on the first armed frame (refused as read, retried, then ok); the heat
              shimmer's two-frame history (frame N's history byte-equal to its unshimmered resolve after the revert,
              frame N+1's resolve reads it)
  armed_refused  as armed with the FP16 refusal staged before the first attach: one engine_plumes_device attached=0
              reason=fp16_blending row, engine_plumes_state reason=fp16_blending glow=native, engine_frame
              forwarded_stage_off=4 on the 40 refused frames, then Reset, attached=1 and armed
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
         'timing': dict(),  # X3M_ENGINE_EFFECTS unset: the default, plumes
         'plumes': dict(X3M_ENGINE_EFFECTS='plumes', X3M_ENGINE_EFFECTS_PRESET='strong', X3M_DEBUG='1'),
         'armed': dict(X3M_ENGINE_EFFECTS='plumes', X3M_ENGINE_EFFECTS_PRESET='strong', X3M_DEBUG='1', X3M_HDR='1', X3M_TAA='1',
                       X3M_MOTION_JITTER='1'),
         'armed_refused': dict(X3M_ENGINE_EFFECTS='plumes', X3M_ENGINE_EFFECTS_PRESET='strong', X3M_DEBUG='1', X3M_HDR='1',
                               X3M_TAA='1', X3M_MOTION_JITTER='1')}
SCENARIO_FRAMES = (1, 2, 3, 5, 6, 7, 8, 9, 10, 11)
OVERFLOW_FRAME, RING = 4, 1024
# The production sources the fixture exercises: their content hashes and the checkout's commit go into the record
# (test_engine_effects compares them with the tree).
PRODUCTION_SOURCES = ('src/proxy/engine_effects.cpp', 'src/proxy/engine_effects.h', 'src/proxy/engine_effects_core.h',
                      'src/proxy/engine_effects_option.h', 'src/proxy/motion_output_engine_effects_inc.h',
                      'src/proxy/motion_output_engine_plumes_inc.h', 'src/proxy/engine_plumes_core.h',
                      'src/renderer/engine_plumes_pass.cpp', 'src/renderer/engine_plumes_pass.h',
                      'src/proxy/motion_output_engine_ribbons_inc.h', 'src/proxy/engine_ribbons_core.h',
                      'src/proxy/motion_output.h', 'src/proxy/capture.cpp', 'src/proxy/engine_far_jets.cpp',
                      'src/proxy/engine_far_jets.h', 'src/proxy/engine_far_jets_core.h', 'src/proxy/engine_nozzle_walk_core.h',
                      'src/proxy/motion_output_engine_shimmer_inc.h', 'src/proxy/engine_shimmer_core.h',
                      'src/renderer/engine_shimmer_pass.cpp', 'src/renderer/engine_shimmer_pass.h')
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


def run(mode, timeout, wine_env=()):
    directory = BUILD / ('engine-effects-' + mode + '-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
    (directory / 'x3m').mkdir(parents=True)
    shutil.copy(EXE, directory)
    shutil.copy(SEAM, directory / 'd3d9.dll')
    (directory / 'x3m/engine_bodies.json').write_text(table())
    env = {k: v for k, v in os.environ.items() if not k.startswith('X3M_')}
    env.update(X3M_FIXTURE_BOTTLE=bottle.BOTTLE, X3M_MOTION_OUTPUT='1', **MODES[mode], **fixture_log.session_log_env(directory))
    command = [bottle.WINE, *bottle.wine_args(), *[a for item in wine_env for a in ('--env', item)], '--dll', 'd3d9=n,b', '--workdir', str(directory), str(directory / EXE.name),
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
                       'plumes': 'armed', 'armed': 'armed', 'armed_refused': 'armed'}[mode]
    if len(modes) != 1 or modes[0].get('status') != expected_status:
        problems.append(f'{mode}: engine_effects_mode {modes}')
    if mode == 'timing':
        if not modes or (modes[0].get('setting'), modes[0].get('mode')) != ('-', 'plumes'):
            problems.append(f'timing: the unset default is not plumes: {modes}')
        out['timing'] = {l.split()[1]: float(fields(l)['median_us_per_draw']) for l in lines if l.startswith('TIMING ')}
        if rows(log, 'engine_draw') or rows(log, 'engine_frame'):
            problems.append('timing: census rows without --debug')
        if len(out['timing']) != 6:
            problems.append(f'timing: {out["timing"]}')
        return problems, out
    plume_rows = rows(log, 'engine_effects_plumes') + rows(log, 'engine_plumes_state') + rows(log, 'engine_stage')
    if mode not in ('plumes', 'armed', 'armed_refused') and plume_rows:
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
    if mode == 'armed':
        # The armed production path: the fixture's CHECK lines carry the per-frame statuses and pixels; the session log
        # carries one attach row, one engine_plumes_failed row per failed stage frame (consecutive 1, 1, 2, 3; the last
        # final=1 retry=reset), the state rows of the cycle and the engine_stage rows of the drawn frames.
        failed_rows = rows(log, 'engine_plumes_failed')
        state = [(r.get('armed'), r.get('reason')) for r in rows(log, 'engine_plumes_state')]
        devices = rows(log, 'engine_plumes_device')
        stage = rows(log, 'engine_stage')
        drawn = [g for g in stage if (g.get('armed'), g.get('ran'), g.get('nozzles'), g.get('skipped_other_view'), g.get('result')) ==
                 ('1', '1', '2', '2', '00000000')]
        out.update(failed_rows=[{k: f.get(k) for k in ('frame', 'result', 'step', 'consecutive', 'until', 'final', 'retry')} for f in failed_rows],
                   state_rows=state, device_rows=len(devices), stage_rows=len(stage), stage_drawn=len(drawn),
                   armed_lines=[fields(l) for l in lines if l.startswith(('ARMED ', 'DISARMED ', 'RESET_CYCLE '))])
        if [(f.get('result'), f.get('step'), f.get('consecutive'), f.get('final'), f.get('retry')) for f in failed_rows] != [
                ('80004005', '5', '1', '0', 'frames'), ('80004005', '5', '1', '0', 'frames'), ('80004005', '5', '2', '0', 'frames'),
                ('80004005', '5', '3', '1', 'reset')]:
            problems.append(f'armed: engine_plumes_failed {out["failed_rows"]}')
        if [(d.get('attached'), d.get('reason')) for d in devices] != [('1', 'ok')]:
            problems.append(f'armed: engine_plumes_device {devices}')
        # The cycle from the first arming: armed / disarmed x3, armed, refused until Reset; after the Reset (any
        # warm-up refusals by path or camera) armed again, the last row.
        warm = {'hdr_taa_path', 'camera', 'lane', 'pending'}
        first = next((i for i, s in enumerate(state) if s == ('1', 'armed')), None)
        cycle = state[first:first + 8] if first is not None else []
        tail = state[first + 8:] if first is not None else []
        if (first is None or any(s[1] not in warm for s in state[:first]) or
                cycle != [('1', 'armed'), ('0', 'disarmed')] * 3 + [('1', 'armed'), ('0', 'failed_until_reset')] or
                not tail or tail[-1] != ('1', 'armed') or any(s[1] not in warm for s in tail[:-1])):
            problems.append(f'armed: engine_plumes_state {state}')
        if len(drawn) < 6:  # first + 3 steady + re-armed + after Reset
            problems.append(f'armed: engine_stage drawn rows {len(drawn)}')
        # The far review fixes: the duplicate frame's row (it has candidates, so it is logged) drops one copy.
        duplicates = [g for g in stage if (g.get('far_jets'), g.get('far_records'), g.get('far_duplicates')) == ('2', '1', '1')]
        out['far_duplicate_rows'] = len(duplicates)
        if len(duplicates) != 1 or any('view_far_total' not in g for g in stage):
            problems.append(f'armed: engine_stage far_duplicates rows {len(duplicates)}')
        # Node-sourced nozzles (engine_nozzle_walk_core.h): the two engine-culled frames' rows carry the handler's culled
        # pair (far_engine 1, node_engine_culled 1), each walks the ship root (node_walked 1) and drops the listed jet
        # (node_dupes 1); no stage row has a node record (nothing resurrected), every row has the node fields.
        culled = [g for g in stage if (g.get('far_engine'), g.get('node_engine_culled')) == ('1', '1')]
        walked = [g for g in stage if g.get('node_walked') not in (None, '0')]
        out['node_engine_culled_rows'] = len(culled)
        out['node_walked_rows'] = [(g.get('node_roots'), g.get('node_walked'), g.get('node_dupes'), g.get('node_records')) for g in walked]
        node_fields = ('node_roots', 'node_records', 'node_dupes', 'node_guard_rejected', 'node_overflow', 'node_root_overflow',
                       'node_not_ship', 'node_walk_us')
        if len(culled) != 2 or any(f not in g for g in stage for f in node_fields) or \
                any(g.get('node_records') != '0' for g in stage) or out['node_walked_rows'] != [('1', '1', '1', '0')] * 2:
            problems.append(f'armed: engine_stage node rows culled {len(culled)} walked {out["node_walked_rows"]}')
        source = rows(log, 'engine_nozzle_source')
        out['nozzle_source_rows'] = [(r.get('source'), r.get('status')) for r in source]
        if not source or any((r.get('source'), r.get('status')) != ('node', 'ok') for r in source):
            problems.append(f'armed: engine_nozzle_source rows {out["nozzle_source_rows"]}')
        if not rows(log, 'motion_output_reset'):
            problems.append('armed: no motion_output_reset row')
        # The game's glow while the stage is off: 3 x 63 disarmed frames and the 70 refused ones forward the four jets.
        frames = rows(log, 'engine_frame')
        stage_off = [f for f in frames if f.get('forwarded_stage_off') not in (None, '0')]
        out['stage_off_frames'] = len(stage_off)
        if len(stage_off) != 3 * 63 + 70 or any((f.get('forwarded_stage_off'), f.get('suppressed'), f.get('records')) != ('4', '0', '0')
                                                for f in stage_off):
            problems.append(f'armed: forwarded_stage_off frames {len(stage_off)}')
        # The travel look (gap 7): the SETA read through the seam. The site's page is unreadable on the first armed frame:
        # that read is refused as `read` (valid 0, not latched; review P6) and the next, after the commit, reads ok; one
        # engage at warp 6, one release after the 0.3 s hold; every drawn stage frame read once, exactly one refused,
        # none invalid.
        seta = [(r.get('event'), r.get('state'), r.get('read'), r.get('valid'), r.get('warp')) for r in rows(log, 'engine_seta')]
        reads = [int(g.get('seta_reads', 0)) for g in stage]
        out.update(seta_rows=seta, seta_reads_last=reads[-1] if reads else None,
                   seta_refused_last=stage[-1].get('seta_refused') if stage else None)
        if seta != [('read', 'off', 'read', '0', '1.000'), ('read', 'off', 'ok', '1', '1.000'), ('engage', 'on', 'ok', '1', '6.000'),
                    ('release', 'off', 'ok', '1', '1.000')]:
            problems.append(f'armed: engine_seta rows {seta}')
        if not stage or (stage[-1].get('seta_refused'), stage[-1].get('seta_invalid'), stage[-1].get('seta_read')) != ('1', '0', 'ok') or \
                not reads or reads[-1] < 10:
            problems.append(f'armed: engine_stage seta counts {out["seta_reads_last"]} {out["seta_refused_last"]}')
        # The heat shimmer's two-frame history (review S1): the fixture's SHIMMER_HISTORY line (its CHECKs carry the
        # verdict) and the session's shimmer rows: attached once, drawn on the probe's frames.
        history = [fields(l) for l in lines if l.startswith('SHIMMER_HISTORY ')]
        shimmer_devices = [(d.get('attached'), d.get('reason')) for d in rows(log, 'engine_shimmer_device')]
        shimmer_drawn = [r for r in rows(log, 'engine_shimmer') if r.get('drew') == '1']
        out.update(shimmer_history=history[-1] if history else None, shimmer_device_rows=shimmer_devices,
                   shimmer_drawn_rows=len(shimmer_drawn))
        if not history or history[-1].get('stage') != '5' or shimmer_devices != [('1', 'ok')] or len(shimmer_drawn) < 2:
            problems.append(f'armed: shimmer history {history} devices {shimmer_devices} drawn rows {len(shimmer_drawn)}')
        return problems, out
    if mode == 'armed_refused':
        # The refusal at the first attach: one refused device row then, after the Reset, one attached row; the state rows
        # name the refusal with glow=native; the refused frames forward the four jets (engine_frame forwarded_stage_off=4).
        devices = [(d.get('attached'), d.get('reason')) for d in rows(log, 'engine_plumes_device')]
        state = [(r.get('armed'), r.get('reason'), r.get('glow')) for r in rows(log, 'engine_plumes_state')]
        frames = rows(log, 'engine_frame')
        stage_off = [f for f in frames if f.get('forwarded_stage_off') not in (None, '0')]
        out.update(device_rows=devices, state_rows=state, stage_off_frames=len(stage_off),
                   armed_lines=[fields(l) for l in lines if l.startswith(('ARMED ', 'REFUSED', 'RESET'))])
        if devices != [('0', 'fp16_blending'), ('1', 'ok')]:
            problems.append(f'armed_refused: engine_plumes_device {devices}')
        if ('0', 'fp16_blending', 'native') not in state or not state or state[-1][:2] != ('1', 'armed'):
            problems.append(f'armed_refused: engine_plumes_state {state}')
        if len(stage_off) != 40 or any((f.get('forwarded_stage_off'), f.get('suppressed'), f.get('records')) != ('4', '0', '0')
                                       for f in stage_off):
            problems.append(f'armed_refused: forwarded_stage_off frames {len(stage_off)}')
        if not rows(log, 'motion_output_reset'):
            problems.append('armed_refused: no motion_output_reset row')
        # No SETA seam here: the tick site does not match in the fixture's memory, the read is refused and fails closed
        # (one read row, never engaged; every read counted refused).
        seta = [(r.get('event'), r.get('state'), r.get('read')) for r in rows(log, 'engine_seta')]
        stage = rows(log, 'engine_stage')
        out.update(seta_rows=seta)
        if seta != [('read', 'off', 'site_mismatch')]:
            problems.append(f'armed_refused: engine_seta rows {seta}')
        drawn_stage = [g for g in stage if g.get('ran') == '1']
        if not drawn_stage or drawn_stage[-1].get('seta_refused') != drawn_stage[-1].get('seta_reads') or drawn_stage[-1].get('seta') != '0':
            problems.append('armed_refused: engine_stage seta counts')
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
    # After flight D: the plume floor's census fields. Node E has no parent and no size; a and b hang under root_a
    # (5 x their size: 5,000 and 1,300 record units; k(5,000) 0.10 gives 500, under a's 1,000; b is RCS); c's root is
    # dirty (radius 0): every value_eff is the record's size.
    out['frame1_floor'] = [(d.get('radius'), d.get('value_eff')) for d in first]
    want_floor = [(0.0, 0.0), (5000.0, 1000.0), (1300.0, 260.0), (0.0, 9366.0)]
    try:
        got_floor = [(float(r), float(v)) for r, v in out['frame1_floor']]
    except (TypeError, ValueError):
        got_floor = None
    if got_floor is None or len(got_floor) != 4 or any(abs(g[0] - w[0]) > 0.01 or abs(g[1] - w[1]) > 0.01 for g, w in zip(got_floor, want_floor)):
        problems.append(f'main: engine_draw radius / value_eff {out["frame1_floor"]}')
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
    parser.add_argument('--wine-env', action='append', default=[], metavar='NAME=VALUE',
                        help="Pass a variable through CrossOver's `wine --env` (applied after the bottle's environment), e.g. "
                             'CX_GRAPHICS_BACKEND=wined3d or =dxvk to select the builtin d3d9 the proxy forwards to for one run')
    parser.add_argument('modes', nargs='*', default=list(MODES))
    args = parser.parse_args()
    for item in args.wine_env:
        if '=' not in item or not item.split('=', 1)[0]:
            parser.error(f'--wine-env expects NAME=VALUE: {item}')
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
              'production_sources': list(PRODUCTION_SOURCES), 'source': source_binding(), 'game_launched': False,
              'wine_env': list(args.wine_env)}
    if not args.no_build:
        record['build'] = build()
    record['binaries'] = {'fixture_sha256': sha(EXE), 'seam_sha256': sha(SEAM), 'dll_sha256': sha(ROOT / 'build/d3d9.dll')}
    problems, runs = [], {}
    for mode in args.modes:
        r = run(mode, args.timeout, args.wine_env)
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
