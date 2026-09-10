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
import subprocess


def main():
    root = Path(__file__).resolve().parents[2]
    executable = root / 'verification/probe/build/temporal_resolve.exe'
    shader = root / 'src/temporal/resolve.hlsl'
    results = root / 'verification/results'
    command = ['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine',
               '--bottle', 'Steam', '--no-update', '--workdir', str(executable.parent),
               str(executable), r'C:\X3\d3dx9_37.dll', 'Z:' + str(shader)]
    environment = os.environ.copy()
    environment['WINEDLLOVERRIDES'] = 'd3d9=b'
    paths = [root / name for name in ('verification/probe/temporal_resolve.cpp',
             'verification/probe/temporal_build.sh', 'verification/probe/temporal_run.py',
             'src/temporal/resolve.h', 'src/temporal/resolve.hlsl')]
    def source_hashes():
        return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    before = source_hashes()
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
    metadata = dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    command=command, process_local_override='d3d9=b', timeout_seconds=60,
                    source_sha256=before, freshly_built=True, build_exit_code=build.returncode,
                    executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest())
    with (results / 'temporal-resolve.txt').open('w') as out, (results / 'temporal-resolve-wine.log').open('w') as err:
        try:
            process = subprocess.run(command, stdout=out, stderr=err, env=environment, timeout=60)
            metadata['exit_code'] = process.returncode
        except subprocess.TimeoutExpired:
            metadata.update(exit_code=None, timed_out=True)
    text = (results / 'temporal-resolve.txt').read_text()
    samples = [line for line in text.splitlines() if line.startswith('SAMPLE ')]
    metadata.update(sample_checks=len(samples),
                    passed_sample_checks=sum(s.endswith(' PASS') for s in samples),
                    reset_passed='RESET PASS' in text,
                    device_generations=text.count('GENERATION '))
    metadata['sources_unchanged_after_run'] = source_hashes() == before
    metadata['executable_unchanged_after_run'] = (
        hashlib.sha256(executable.read_bytes()).hexdigest() == metadata['executable_sha256'])
    metadata['passed'] = (metadata['exit_code'] == 0 and len(samples) == 58
                          and metadata['sources_unchanged_after_run']
                          and metadata['executable_unchanged_after_run']
                          and metadata['passed_sample_checks'] == 58
                          and metadata['reset_passed'] and metadata['device_generations'] == 2
                          and 'RESULT PASS samples=58 generations=2' in text)
    (results / 'temporal-resolve-summary.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(json.dumps(metadata, indent=2))
    return 0 if metadata['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
