#!/usr/bin/env python3
"""Run the bounded original GPU temporal fixture in CrossOver Preview.

Production shader source is loaded directly, so the evidence is not from a test
reimplementation. All inputs and numeric expected results are original synthetic
fixtures. No game launch/install or settings changes occur.
"""
import datetime
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory


def fixture_children(shader):
    """Only this fixture's actual Unix child, not Wine, its server or launcher."""
    listing = subprocess.check_output(['ps', '-axo', 'pid=,stat=,args='], text=True)
    found = {}
    for row in listing.splitlines():
        fields = row.split(None, 2)
        if len(fields) != 3 or 'Z' in fields[1]:
            continue
        pid, _, command = fields
        executable = command.split(None, 1)[0].replace('\\', '/').lower()
        # This fixture has exactly the compiler path and shader path arguments;
        # ps prints the latter last. Do not accept a shader-name prefix match.
        if executable.endswith('/temporal_resolve.exe') and command.rstrip().endswith(' Z:' + str(shader)):
            found[int(pid)] = command
    return found


def drain_fixture(shader):
    """Keep the caller's Wine lock until the timed-out child has really exited.

    CrossOver's launcher may die while its Windows child is still compiling.
    Admission checks below require no pre-existing matching fixture. Before each
    signal recheck both PID and the exact command to avoid acting on PID reuse.
    """
    started = time.monotonic()
    signalled = []
    for grace, action in ((15, None), (10, signal.SIGTERM), (None, signal.SIGKILL)):
        if action is not None:
            for pid, command in fixture_children(shader).items():
                if fixture_children(shader).get(pid) == command:
                    try:
                        os.kill(pid, action)
                        signalled.append({'pid': pid, 'signal': int(action)})
                    except ProcessLookupError:
                        pass
        deadline = None if grace is None else time.monotonic() + grace
        while fixture_children(shader):
            if deadline is not None and time.monotonic() >= deadline:
                break
            # After SIGKILL retain the lock until the child is gone; never hand
            # another fixture a live backend from this timed-out run.
            time.sleep(1)
        else:
            return dict(drained=True, seconds=round(time.monotonic()-started, 3),
                        signalled=signalled)


def main():
    root = Path(__file__).resolve().parents[2]
    executable = root / 'verification/probe/build/temporal_resolve.exe'
    shader = root / 'src/temporal/resolve.hlsl'
    results = bottle.results_dir(root)
    command = ['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine',
               '--bottle', bottle.BOTTLE, '--no-update', '--workdir', str(executable.parent),
               str(executable), r'C:\X3\d3dx9_37.dll', 'Z:' + str(shader)]
    environment = os.environ.copy()
    environment['WINEDLLOVERRIDES'] = 'd3d9=b'
    paths = [root / name for name in ('verification/probe/temporal_resolve.cpp',
             'verification/probe/temporal_build.sh', 'verification/probe/temporal_run.py',
             'src/temporal/resolve.h', 'src/temporal/resolve.hlsl')]
    def source_hashes():
        return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    before = source_hashes()
    if fixture_children(shader):
        raise RuntimeError('An existing temporal fixture must finish before this run')
    # The recorded source must be the source that produced the tested executable.
    # Always build; never trust a stale build directory from a previous iteration.
    build = subprocess.run([str(root / 'verification/probe/temporal_build.sh')],
                           cwd=root, capture_output=True, text=True)
    if build.stdout:
        print(build.stdout, end='')
    if build.stderr:
        print(build.stderr, end='')
    if build.returncode or source_hashes() != before:
        failure = dict(passed=False, build_exit_code=build.returncode,
                       reason='Build failed or source changed during compilation',
                       source_sha256_before=before, source_sha256_after=source_hashes())
        (results / 'temporal-resolve-summary.json').write_text(json.dumps(failure, indent=2) + '\n')
        print(json.dumps(failure, indent=2))
        return 1
    metadata = dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(), bottle=bottle.describe(),
                    # Two complete shader compilations (one per device generation)
                    # can exhaust 60 seconds on the Rosetta fixture runtime.
                    command=command, process_local_override='d3d9=b', timeout_seconds=180,
                    source_sha256=before, freshly_built=True, build_exit_code=build.returncode,
                    executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest())
    with (results / 'temporal-resolve.txt').open('w') as out, (results / 'temporal-resolve-wine.log').open('w') as err:
        try:
            process = subprocess.run(command, stdout=out, stderr=err, env=environment, timeout=180)
            metadata['exit_code'] = process.returncode
        except subprocess.TimeoutExpired:
            metadata.update(exit_code=None, timed_out=True)
            metadata['timeout_cleanup'] = drain_fixture(shader)
    text = (results / 'temporal-resolve.txt').read_text()
    metadata['report_sha256'] = hashlib.sha256((results / 'temporal-resolve.txt').read_bytes()).hexdigest()
    metadata['wine_log_sha256'] = hashlib.sha256((results / 'temporal-resolve-wine.log').read_bytes()).hexdigest()
    samples = [line for line in text.splitlines() if line.startswith('SAMPLE ')]
    metadata.update(sample_checks=len(samples),
                    passed_sample_checks=sum(s.endswith(' PASS') for s in samples),
                    reset_passed='RESET PASS' in text,
                    device_generations=text.count('GENERATION '))
    metadata['sources_unchanged_after_run'] = source_hashes() == before
    metadata['executable_unchanged_after_run'] = (
        hashlib.sha256(executable.read_bytes()).hexdigest() == metadata['executable_sha256'])
    metadata['passed'] = (metadata['exit_code'] == 0 and len(samples) == 78
                          and metadata['sources_unchanged_after_run']
                          and metadata['executable_unchanged_after_run']
                          and metadata['passed_sample_checks'] == 78
                          and metadata['reset_passed'] and metadata['device_generations'] == 2
                          and 'RESULT PASS samples=78 generations=2' in text)
    (results / 'temporal-resolve-summary.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(json.dumps(metadata, indent=2))
    return 0 if metadata['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
