#!/usr/bin/env python3
"""Per-routed-draw CPU cost of the proxy on the fixture device; no game launch.

Runs verification/probe/build/motion_output_fixture.exe in its "routebench"
mode (400 consecutive routed scene draws of the reviewed pair per frame, twelve
frames, objects A/B alternating so every draw matches history) under the seam
DLL in several route configurations and reports the wall time of the
DrawPrimitive call per draw (median over frames 2-11; the QPC pair's own cost
is measured in the same process and printed beside it) and, from the DLL's own
frame line, the per-frame route counters (routed, set_rt, rs_gets, gate_us,
route_draw_us). Configurations:

  off                    X3M_MOTION_OUTPUT=0: the proxy's draw hook without the route (baseline)
  perdraw                the production route (per-draw RT binds, hybrid unhook, no per-draw stamps)
  perdraw-telemetry-draw the same with X3M_TELEMETRY_DRAW=1 (run129's launch: ~18 QPC stamps per routed draw)
  perdraw-shadow         the same with X3M_STATE_SHADOW=1 (setter hooks on, state answered from the shadow)
  lazy                   X3M_MOTION_RT_MODE=lazy (keeps RT1/RT2 across the run, never the write masks: no setter hooks)
  lazy-ownership         the same through the ownership wrapper
  perdraw-masked / lazy-masked  X3M_FIXTURE_BENCH_MASK=1: the application holds COLORWRITEENABLE1/2 = 7 over the
                         run, so every lazy routed draw takes the mask write/restore fallback
  perdraw-cascades       the production route with the depth replay, candidates, five cascades and
                         caster retention on (the ownership wrapper for the retention journal)

The difference perdraw - off is the proxy's per-routed-draw cost; the native
DrawPrimitive and the hook envelope are in both. Results go to
verification/results/bottle-X3/route-bench-<label>.json.

Run it under the Wine lock:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py \\
      python3 verification/probe/run_route_bench.py --label before [--seam build/motion-output-seam/d3d9.dll]
"""
import argparse
import datetime
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import bottle  # noqa: E402
from game_guard import game_running  # noqa: E402

PROBE = ROOT / 'verification/probe'
BUILD = PROBE / 'build'
EXE = BUILD / 'motion_output_fixture.exe'
SEAM = BUILD / 'motion-output-seam/d3d9.dll'
RESULTS = bottle.results_dir(ROOT)
WINE = Path(bottle.WINE)
RAW = [Path('/tmp/x3-shader-sweep/programs/vs_53a0a641107ed76c.bin'), Path('/tmp/x3-shader-sweep/programs/ps_8759c7838bbc86c2.bin')]

BASE_ENV = dict(
    X3M_CAMERA='vanilla', X3M_CHASE_SCENE_FIX='0', X3M_CHASE_COMBAT_TIGHTNESS='0',
    X3M_MOTION_OUTPUT='1', X3M_MOTION_JITTER='1', X3M_MOTION_JITTER_SAMPLES='8', X3M_TAA='0', X3M_TAA_DEBUG='0',
    X3M_CAPTURE_START='1000', X3M_CAPTURE_FRAMES='0', X3M_TELEMETRY='1', X3M_TELEMETRY_DRAW='0',
    X3M_FIXTURE_CAMERA='rotate', X3M_TAA_SENTINEL='auto', X3M_FIXTURE_WRAP='0',
    X3M_MOTION_RT_MODE='perdraw', X3M_MOTION_FRAME_LOG='1', X3M_SCENE_HOOK='0', X3M_HDR='0',
    X3M_HDR_EXPOSURE='fixed', X3M_HDR_EV_MANUAL='',
    X3M_OWNERSHIP='0', X3M_DEPTH_COPY='0', X3M_SCENE_DEPTH_CAPTURE='0', X3M_OBJECT_TRACE='0', X3M_OBJECT_LIFETIME='0',
    X3M_MESH_CACHE='0', X3M_ADMISSION='0', X3M_FINITE_POSITIONS='0', X3M_MOTION_CAPTURE='0',
    X3M_CRYPT_CACHE='0', X3M_LOADING_PROBES='0', X3M_MESH_ADJACENCY='native', X3M_MESH_ADJACENCY_DUMP='0',
    X3M_RESOURCE_READ='native', X3M_DAT_HANDLES='0', X3M_GZ_BUFFER='0', X3M_GZ_BUFFER_KB='256',
    X3M_LINEAR_MATERIALS='0', X3M_LINEAR_EMISSIONS='0', X3M_SCREEN_EMISSION='0',
    X3M_SHADOW_REPLAY_EXTENT='250', X3M_SHADOW_REPLAY_DEPTH_HALF='512', X3M_SHADOW_REPLAY_CAP='512',
    X3M_SHADOW_CASCADES='0', X3M_FIXTURE_SUNAPPLY_CASCADES='0', X3M_FIXTURE_BENCH_MASK='0',
    X3M_SHADOW_RETENTION_CENSUS='0', X3M_SHADOW_CASTER_RETENTION='0', X3M_SHADOW_RETENTION_TIMING='0')
UNSET = ('X3M_STATE_SHADOW', 'X3M_SHADOW_CASCADE_SIZES', 'X3M_SHADOW_CASCADE_CAPS', 'X3M_SHADOW_CASCADE_BUDGET',
         'X3M_FIXTURE_SHADOW_CASCADES', 'X3M_SHADOW_CASTER_RETENTION_AGE', 'X3M_SHADOW_CASTER_RETENTION_EPS',
         'X3M_SUN_SHADOW_RECEIVER_DEPTH', 'X3M_TAA_SHARPEN', 'X3M_TAA_MIP_BIAS')
CASCADES = dict(X3M_OWNERSHIP='1', X3M_SHADOW_REPLAY_DEPTH='1', X3M_SHADOW_REPLAY_CANDIDATES='1', X3M_SHADOW_REPLAY_SIZE='256',
                X3M_FIXTURE_SLICE_NEAR='0.5', X3M_SHADOW_CASCADES='250,1500,4000,12000,30000', X3M_SHADOW_CASCADE_DROP_ORDER='importance',
                X3M_SHADOW_CASTER_RETENTION='1')
DEPTH = dict(X3M_OWNERSHIP='1', X3M_SHADOW_REPLAY_DEPTH='1', X3M_SHADOW_REPLAY_SIZE='256', X3M_FIXTURE_SLICE_NEAR='0.5')
CONFIGS = [
    ('off', dict(X3M_MOTION_OUTPUT='0')),
    ('perdraw', {}),
    ('perdraw-telemetry-draw', dict(X3M_TELEMETRY_DRAW='1')),
    ('perdraw-shadow', dict(X3M_STATE_SHADOW='1')),
    ('lazy', dict(X3M_MOTION_RT_MODE='lazy')),
    ('lazy-ownership', dict(X3M_MOTION_RT_MODE='lazy', X3M_OWNERSHIP='1')),
    ('perdraw-masked', dict(X3M_FIXTURE_BENCH_MASK='1')),
    ('lazy-masked', dict(X3M_MOTION_RT_MODE='lazy', X3M_FIXTURE_BENCH_MASK='1')),
    ('perdraw-cascades', CASCADES),
    # Attribution: the jitter's two constant writes, RT2 (two binds, two masks, one read),
    # the ownership wrapper alone, the single-map depth replay (lease per draw), the
    # cascades without retention, and the retention's own per-draw timing line.
    ('perdraw-nojitter', dict(X3M_MOTION_JITTER='0')),
    ('perdraw-nodepth', dict(X3M_FIXTURE_MOTION_DEPTH='0')),
    ('off-ownership', dict(X3M_MOTION_OUTPUT='0', X3M_OWNERSHIP='1')),
    ('perdraw-ownership', dict(X3M_OWNERSHIP='1')),
    ('perdraw-depth', DEPTH),
    ('perdraw-cascades-noretention', dict(CASCADES, X3M_SHADOW_CASTER_RETENTION='0')),
    ('perdraw-cascades-timing', dict(CASCADES, X3M_SHADOW_RETENTION_TIMING='1')),
]
RETENTION_FIELDS = ('records', 'nodes_live', 'us', 'journal_us', 'walk_us', 'draw_us', 'draw_calls', 'gate_us')
FRAME_FIELDS = ('draws', 'routed', 'matched', 'set_rt', 'lazy_flushes', 'lazy_mask_writes', 'jitter_writes', 'rs_mode', 'rs_queries', 'rs_hits', 'rs_gets',
                'state_shadow', 'gate_us', 'route_draw_us', 'set_rt_us', 'lazy_flush_us', 'jitter_us', 'gate1', 'gate2', 'gate3', 'gate4', 'gate5', 'gate6')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fields(line):
    return dict(token.split('=', 1) for token in line.split()[1:] if '=' in token)


def run_config(name, extra, seam, exe, draws, log):
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    directory = BUILD / f'route-bench-{name}-{stamp}'
    directory.mkdir(parents=True)
    shutil.copy(exe, directory / EXE.name)
    shutil.copy(seam, directory / 'd3d9.dll')
    env = dict(os.environ, **BASE_ENV)
    for key in UNSET:
        env.pop(key, None)
    env.update(extra)
    command = [str(WINE)] + bottle.wine_args() + ['--dll', 'd3d9=n,b', '--workdir', str(directory), str(directory / EXE.name)]
    command += ['Z:' + str(p) for p in RAW] + ['routebench', str(draws)]
    assert not game_running(), 'the game is running'
    log.write(f'==== {name}\n')
    log.flush()
    completed = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=log, text=True, timeout=900)
    text = completed.stdout
    (directory / 'stdout.txt').write_text(text)
    assert completed.returncode == 0, f'{name}: exit {completed.returncode}\n{text[-2000:]}'
    summary = [fields(l) for l in text.splitlines() if l.startswith('ROUTEBENCH_SUMMARY ')]
    assert len(summary) == 1, f'{name}: no summary\n{text[-2000:]}'
    summary = {k: float(v) if k != 'timing' else v for k, v in summary[0].items()}
    traces = list((directory / 'x3-modern-captures').glob('session-*.log'))
    frames, retention = [], []
    hooks = None
    for line in (traces[0].read_text().splitlines() if traces else []):
        if line.startswith('motion_output_frame '):
            f = fields(line)
            frames.append({k: f.get(k) for k in FRAME_FIELDS})
        elif line.startswith('shadow_retention_frame '):
            f = fields(line)
            retention.append({k: f.get(k) for k in RETENTION_FIELDS})
        elif line.startswith('state_hooks '):
            hooks = fields(line)
    # The measured frames (2-11): the DLL's frame line of the last measured frame.
    last = frames[-2] if len(frames) >= 2 else (frames[-1] if frames else {})
    last_retention = retention[-2] if len(retention) >= 2 else (retention[-1] if retention else {})
    return dict(env=extra, directory=str(directory), summary=summary, frame=last, retention=last_retention, frames_logged=len(frames), state_hooks=hooks)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--seam', type=Path, default=SEAM, help='fixture-seam DLL (default: the fresh build under verification/probe/build)')
    parser.add_argument('--fixture', type=Path, default=EXE)
    parser.add_argument('--draws', type=int, default=400)
    parser.add_argument('--label', required=True, help='result file suffix (route-bench-<label>.json)')
    parser.add_argument('--configs', nargs='*', default=[c for c, _ in CONFIGS])
    args = parser.parse_args()
    for p in RAW:
        assert p.is_file(), f'missing shader input {p}'
    result = dict(bottle=bottle.BOTTLE, bottle_label=bottle.label(),
                  seam_sha256=sha(args.seam), fixture_sha256=sha(args.fixture), draws=args.draws,
                  started=datetime.datetime.now().isoformat(timespec='seconds'), configs={})
    with (RESULTS / f'route-bench-{args.label}-wine.log').open('w') as log:
        for name, extra in CONFIGS:
            if name not in args.configs:
                continue
            result['configs'][name] = run_config(name, extra, args.seam, args.fixture, args.draws, log)
            s, f = result['configs'][name]['summary'], result['configs'][name]['frame']
            print(f"{name:24s} draw_us median={s['draw_us_median']:.3f} min={s['draw_us_min']:.3f} run_us={s['run_us_median']:.3f} qpc_pair={s['qpc_pair_us']:.3f}"
                  f"  routed={f.get('routed')} set_rt={f.get('set_rt')} flushes={f.get('lazy_flushes')} rs_mode={f.get('rs_mode')} rs_gets={f.get('rs_gets')}"
                  f" gate_us={f.get('gate_us')} route_draw_us={f.get('route_draw_us')} set_rt_us={f.get('set_rt_us')} jitter_us={f.get('jitter_us')}"
                  + (f"  retention: {result['configs'][name]['retention']}" if result['configs'][name]['retention'] else ''))
    off = result['configs'].get('off')
    if off:
        base = off['summary']['draw_us_median']
        for name, c in result['configs'].items():
            c['proxy_us_per_draw'] = round(c['summary']['draw_us_median'] - base, 3)
            print(f"{name:24s} proxy_us_per_draw={c['proxy_us_per_draw']:.3f}")
    out = RESULTS / f'route-bench-{args.label}.json'
    out.write_text(json.dumps(result, indent=1))
    print('wrote', out)


if __name__ == '__main__':
    main()
