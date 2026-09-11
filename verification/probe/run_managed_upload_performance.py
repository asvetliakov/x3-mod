#!/usr/bin/env python3
"""Same-work CPU mapping/upload timing through public native and wrapped D3D9."""
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
FILES = [
    'src/ownership/d3d9_ownership.h', 'src/ownership/d3d9_ownership.cpp',
    'src/ownership/execution_state.cpp', 'src/ownership/execution_state.h',
    'src/ownership/d3d9_classes_inc.h', 'src/ownership/d3d9_forwarders_inc.h',
    'src/ownership/finite_buffer_evidence.h', 'src/ownership/finite_buffer_evidence.cpp',
    'src/ownership/portable_managed_upload.h', 'src/ownership/portable_managed_upload.cpp',
    'tools/ownership/generate_d3d9_forwarders.py',
    'verification/probe/managed_upload_performance.cpp',
    'verification/probe/build_managed_upload_performance.sh',
    'verification/probe/run_managed_upload_performance.py',
]
NATIVE_ROOT = Path('/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/lib/wine/i386-windows')
EXE = ROOT / 'verification/probe/build/managed_upload_performance.exe'
RESULTS = ROOT / 'verification/results'
MODES = ('native_writeonly', 'native_readable', 'wrapped_off', 'wrapped_finite')
KINDS = ('vertex', 'index16', 'index32')
SIZES = (65536, 1048576, 8388608)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def hashes():
    return {name: digest(ROOT / name) for name in FILES} | {
        'native/' + name: digest(NATIVE_ROOT / name) for name in ('d3d9.dll', 'wined3d.dll')}


def parse_report(text):
    lines = text.splitlines()
    result = re.fullmatch(r'RESULT PASS samples=36 checks=2768', text.rstrip().splitlines()[-1])
    if not result or sum(line.startswith('RESULT ') for line in lines) != 1 or 'FAIL' in text:
        raise ValueError('Missing, duplicate or incorrect terminal result')
    samples = {}
    pattern = r'SAMPLE mode=(\w+) kind=(\w+) bytes=(\d+) repeats=7 create_us=([\d.]+) lock_us=([\d.]+) copy_us=([\d.]+) unlock_us=([\d.]+)'
    for line in lines[:-1]:
        match = re.fullmatch(pattern, line)
        if not match:
            raise ValueError('Unexpected sample output: ' + line)
        mode, kind, size, *times = match.groups()
        key = (mode, kind, int(size))
        if key in samples:
            raise ValueError('Duplicate sample')
        values = [float(value) for value in times]
        if any(not math.isfinite(value) or value < 0 for value in values):
            raise ValueError('Invalid timing')
        samples[key] = dict(mode=mode, kind=kind, bytes=int(size), repeats=7,
                            **dict(zip(('create_us', 'lock_us', 'copy_us', 'unlock_us'), values)))
    expected = {(mode, kind, size) for mode in MODES for kind in KINDS for size in SIZES}
    if set(samples) != expected:
        raise ValueError('Incomplete sample inventory')
    return list(samples.values())


def main():
    summary = RESULTS / 'managed-upload-performance-summary.json'
    meta = dict(passed=False, phase='building', fresh_build=False, game_launched=False,
                started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    def save():
        summary.write_text(json.dumps(meta, indent=2) + '\n')
    save()
    try:
        processes = subprocess.run(['pgrep', '-ifl', 'X3AP.exe'], capture_output=True, text=True)
        if processes.returncode not in (0, 1) or processes.stdout.strip():
            raise RuntimeError('Game process present or inventory failed: ' + processes.stdout)
        before = hashes()
        meta['source_hashes_before_build'] = before
        save()
        with (RESULTS / 'managed-upload-performance-build.txt').open('wb') as output:
            subprocess.run(['sh', 'verification/probe/build_managed_upload_performance.sh'], cwd=ROOT,
                           stdout=output, stderr=subprocess.STDOUT, check=True, timeout=90)
        meta['source_hashes_after_build'] = hashes()
        if before != meta['source_hashes_after_build']:
            raise RuntimeError('Sources changed during build')
        meta.update(fresh_build=True, phase='running', executable_sha256=digest(EXE))
        command = ['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine',
                   '--bottle', 'Steam', '--no-update', '--dll', 'd3d9=b', '--workdir', str(EXE.parent), str(EXE)]
        meta['command'] = command
        save()
        report = RESULTS / 'managed-upload-performance.txt'
        wine = RESULTS / 'managed-upload-performance-wine.log'
        with report.open('wb') as output, wine.open('wb') as error:
            run = subprocess.run(command, cwd=ROOT, stdout=output, stderr=error,
                                 env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'), timeout=120)
        meta.update(exit_code=run.returncode, source_hashes_after=hashes(),
                    report_sha256=digest(report), wine_log_sha256=digest(wine),
                    binary_unchanged=digest(EXE) == meta['executable_sha256'])
        meta['samples'] = parse_report(report.read_text())
        if run.returncode or before != meta['source_hashes_after'] or not meta['binary_unchanged']:
            raise RuntimeError('Execution failed or source/executable changed')
        meta.update(passed=True, phase='complete', checks=2768, limits=[
            'Medians of seven new-buffer trials after two warmups for every mode/kind/size.',
            'Four modes use identical public MANAGED Create/Lock/NOSYSLOCK/memset/Unlock work; no WRITEONLY reads.',
            'Creation and Lock/Unlock CPU costs only; no draws, first-use GPU upload, GPU completion, Windows run or game loading claim.',
            'Buffer descriptor and finite/index-result verification are outside timed intervals.',
            'Native DLL digests record before/after stability only; no allowlist or production identity gate.',
        ])
    except (Exception, KeyboardInterrupt) as error:
        meta.update(passed=False, phase='failed', error=repr(error))
    save()
    print(json.dumps({key: meta.get(key) for key in ('passed', 'phase', 'checks', 'error')}, indent=2))
    return 0 if meta['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
