#!/usr/bin/env python3
"""Parallel front end for the canonical host suite.

Runs exactly the modules that

    PYTHONPATH=verification/probe python3 -m unittest discover -s verification/analysis -p 'test_*.py'

discovers, one ``python -m unittest <module>`` worker process per module, longest
module first from a checked-in duration hint. The worker environment reproduces
the canonical command: cwd is the repository root, ``verification/probe`` and
``verification/analysis`` are on ``PYTHONPATH`` (discovery puts the start
directory on ``sys.path`` and the repository root is ``sys.path[0]`` of
``-m unittest``), and the interpreter is ``sys.executable`` -- run this script
with the interpreter that has NumPy, ``/usr/bin/python3`` on the project Mac.

Retired feature tests (``verification/analysis/retired_tests.py``, the linear
material conversion and the rejected full-surface emission bracket) are skipped
by default here and by the canonical discover command; ``--include-retired``
runs them. The retire/keep decision per module is in
``docs/verification/host-suite.md``.

Exit code is non-zero if any module fails, errors, or runs zero tests.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ANALYSIS = ROOT / 'verification/analysis'
PROBE = ROOT / 'verification/probe'
DURATIONS = PROBE / 'host_suite_durations.json'
DEFAULT_DURATION = 10.0  # an unmeasured module is scheduled early, not last
MAX_JOBS = 12  # compile-heavy modules; more workers only add scheduler and memory pressure

sys.path.insert(0, str(ROOT))
from verification.analysis.retired_tests import RETIRED  # noqa: E402

# Modules that must not run concurrently with each other: every one of them runs
# `tools/manage.py launch`, which takes the exclusive installer flock on the game
# directory (media_package.installer_lock) for the whole validation, including
# --dry-run. Two at once and the loser exits with "another installer or launcher
# is active". They share one lane of the pool, dispatched first so the lane is
# not the tail of the run. Nothing else collides: no analysis module writes into
# the repository tree, verification/results or a fixed temp path -- each builds
# and runs its host fixtures inside its own temporary directory (audited
# 2026-09-21).
SERIAL_GROUP = frozenset({
    'test_chase_camera',
    'test_collide_box_cull',
    'test_collide_memo',
    'test_collide_narrow_census',
    'test_collide_sat_sse2',
    'test_comparison_hotkeys',
    'test_cull_census',
    'test_cull_small_parts',
    'test_fps_overlay',
    'test_launcher_stderr_tee',
    'test_linear_emission_hull_gain',
    'test_linear_emission_source_gain',
    'test_linear_material_live',
    'test_lod_scale_launch',
    'test_media_package',
    'test_motion_cut_defaults',
    'test_original_fill',
    'test_point_light_admission',
    'test_screen_emission_live',
    'test_shadow_cascades',
    'test_shadow_replay_candidates',
    'test_shadow_replay_depth',
    'test_shadow_retention',
    'test_sun_share_lane',
    'test_taa_image_defaults',
    'test_voice_decoder_launch',
    'test_volumetric_fog',
})

RAN = re.compile(r'^Ran (\d+) tests? in ([0-9.]+)s', re.M)


def modules(include_retired):
    names = sorted(p.stem for p in ANALYSIS.glob('test_*.py'))
    if include_retired:
        return names
    return [n for n in names if n not in RETIRED]


def environment(include_retired):
    env = dict(os.environ)
    parts = [str(PROBE), str(ANALYSIS)]
    if env.get('PYTHONPATH'):
        parts.append(env['PYTHONPATH'])
    env['PYTHONPATH'] = os.pathsep.join(parts)
    if include_retired:
        env['X3M_INCLUDE_RETIRED'] = '1'
    else:
        env.pop('X3M_INCLUDE_RETIRED', None)
    return env


def run_module(name, env):
    start = time.monotonic()
    done = subprocess.run([sys.executable, '-m', 'unittest', name],
                          cwd=str(ROOT), env=env, capture_output=True, text=True)
    wall = time.monotonic() - start
    output = done.stdout + done.stderr
    match = RAN.search(output)
    tests = int(match.group(1)) if match else 0
    ok = done.returncode == 0 and match is not None and tests > 0
    reason = ''
    if done.returncode != 0:
        reason = 'failed'
    elif match is None:
        reason = 'no unittest summary'
    elif tests == 0:
        reason = 'ran zero tests'
    return dict(module=name, tests=tests, wall=wall, ok=ok, reason=reason, output=output)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--jobs', '-j', type=int, default=None, help='worker processes (default: CPU count capped at %d)' % MAX_JOBS)
    parser.add_argument('--serial', action='store_true', help='one module at a time, for a reference run')
    parser.add_argument('--include-retired', action='store_true', help='also run the retired feature modules')
    parser.add_argument('--modules', nargs='+', metavar='M', help='run only these modules (retired ones are allowed)')
    parser.add_argument('--write-durations', action='store_true', help='update %s from this run' % DURATIONS.name)
    parser.add_argument('--list', action='store_true', help='print the selected modules and exit')
    parser.add_argument('--slowest', type=int, default=15, help='how many slow modules to report (default 15)')
    args = parser.parse_args(argv)

    selected = args.modules if args.modules else modules(args.include_retired)
    missing = [n for n in selected if not (ANALYSIS / (n + '.py')).exists()]
    if missing:
        parser.error('unknown module(s): ' + ', '.join(missing))
    if args.list:
        print('\n'.join(selected))
        return 0

    include_retired = args.include_retired or bool(args.modules and set(args.modules) & RETIRED)
    env = environment(include_retired)
    hints = {}
    if DURATIONS.exists():
        hints = json.loads(DURATIONS.read_text()).get('modules', {})
    # Longest first, and the shared-lane modules before the rest so the lane
    # drains while the other workers take the long independent modules.
    order = sorted(selected, key=lambda n: (n not in SERIAL_GROUP, -hints.get(n, DEFAULT_DURATION), n))
    jobs = 1 if args.serial else (args.jobs or min(os.cpu_count() or 4, MAX_JOBS))
    jobs = max(1, min(jobs, len(order)))

    print('host suite: %d modules, %d job%s, %s retired, %s'
          % (len(order), jobs, '' if jobs == 1 else 's',
             'with' if include_retired else 'without', sys.executable), flush=True)

    results = []
    pending = list(order)
    index = threading.Lock()
    serial_lane = threading.Lock()
    started = time.monotonic()

    def take():
        """Next runnable module, plus whether this worker holds the shared lane.
        A worker never blocks on the lane: it takes independent work instead."""
        while True:
            with index:
                if not pending:
                    return None, False
                for position, name in enumerate(pending):
                    if name not in SERIAL_GROUP:
                        return pending.pop(position), False
                    if serial_lane.acquire(blocking=False):
                        return pending.pop(position), True
                # Only lane modules are left and the lane is busy.
            time.sleep(0.1)

    def worker():
        while True:
            name, lane = take()
            if name is None:
                return
            try:
                result = run_module(name, env)
            finally:
                if lane:
                    serial_lane.release()
            with index:
                results.append(result)
                print('%s %-52s %5d tests %7.1fs' % ('ok  ' if result['ok'] else 'FAIL', name, result['tests'], result['wall']), flush=True)

    threads = [threading.Thread(target=worker, daemon=True) for _ in range(jobs)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    wall = time.monotonic() - started

    results.sort(key=lambda r: -r['wall'])
    failures = [r for r in results if not r['ok']]
    tests = sum(r['tests'] for r in results)

    if failures:
        for r in failures:
            print('\n' + '=' * 72)
            print('%s: %s' % (r['module'], r['reason']))
            lines = r['output'].splitlines()
            if len(lines) > 400:
                print('... %d earlier lines omitted ...' % (len(lines) - 400))
                lines = lines[-400:]
            print('\n'.join(lines))
    print('\nslowest %d modules:' % args.slowest)
    for r in results[:args.slowest]:
        print('  %7.1fs  %-52s %5d tests' % (r['wall'], r['module'], r['tests']))
    print('\n%d modules, %d tests, %d failing module(s), %.1fs wall (%s)'
          % (len(results), tests, len(failures), wall, 'serial' if jobs == 1 else '%d jobs' % jobs))

    if args.write_durations:
        recorded = dict(hints)
        recorded.update({r['module']: round(r['wall'], 1) for r in results if r['ok']})
        DURATIONS.write_text(json.dumps(dict(
            comment='Per-module wall seconds, a scheduling hint only; refresh with run_host_suite.py --write-durations.',
            modules=dict(sorted(recorded.items()))), indent=1) + '\n')
        print('wrote %s' % DURATIONS)

    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
