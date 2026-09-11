#!/usr/bin/env python3
"""Fresh-build provenance for the actual native/wrapped geometry-lease observer."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
FILES = [
    'src/ownership/d3d9_ownership.h', 'src/ownership/d3d9_ownership.cpp', 'src/ownership/application_admission.h', 'src/ownership/application_admission.cpp', 'src/ownership/application_admission_abi.h', 'src/ownership/application_admission_abi.cpp', 'src/ownership/execution_state.cpp', 'src/ownership/execution_state.h',
    'src/ownership/d3d9_classes_inc.h', 'src/ownership/d3d9_forwarders_inc.h',
    'src/ownership/finite_buffer_evidence.h', 'src/ownership/finite_buffer_evidence.cpp',
    'src/ownership/portable_managed_upload.h', 'src/ownership/portable_managed_upload.cpp',
    'tools/ownership/generate_d3d9_forwarders.py',
    'verification/probe/geometry_lease_fixture.cpp', 'verification/probe/build_geometry_lease.sh', 'verification/probe/build_admission_dependencies.sh',
    'verification/probe/run_geometry_lease.py',
]
NATIVE_ROOT = Path('/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/lib/wine/i386-windows')
# Runtime identity is recorded for reproducibility, never an admission allowlist.
NATIVE = ('d3d9.dll', 'wined3d.dll')
EXE = ROOT / 'verification/probe/build/geometry_lease_fixture.exe'
RESULTS = ROOT / 'verification/results'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def hashes():
    return {name: digest(ROOT / name) for name in FILES} | {
        'native/' + name: digest(NATIVE_ROOT / name) for name in NATIVE}


def main():
    summary = RESULTS / 'geometry-lease-summary.json'
    meta = dict(passed=False, phase='building', fresh_build=False, game_launched=False,
                started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    def save():
        summary.write_text(json.dumps(meta, indent=2) + '\n')
    save()
    try:
        processes = subprocess.run(['pgrep', '-ifl', 'X3AP.exe'], capture_output=True, text=True)
        if processes.returncode not in (0, 1) or processes.stdout.strip():
            raise RuntimeError('Game process present or process inventory failed: ' + processes.stdout)
        before = hashes()
        meta['source_hashes_before_build'] = before
        save()
        with (RESULTS / 'geometry-lease-build.txt').open('wb') as output:
            subprocess.run(['sh', 'verification/probe/build_geometry_lease.sh'], cwd=ROOT,
                           stdout=output, stderr=subprocess.STDOUT, check=True, timeout=90)
        meta['source_hashes_after_build'] = hashes()
        if before != meta['source_hashes_after_build']:
            raise RuntimeError('Sources changed during build')
        meta.update(fresh_build=True, phase='running', executable_sha256=digest(EXE))
        command = ['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine',
                   '--bottle', 'Steam', '--no-update', '--dll', 'd3d9=b',
                   '--workdir', str(EXE.parent), str(EXE)]
        meta['command'] = command
        save()
        processes = subprocess.run(['pgrep', '-ifl', 'X3AP.exe'], capture_output=True, text=True)
        if processes.returncode not in (0, 1) or processes.stdout.strip():
            raise RuntimeError('Game process present before runtime: ' + processes.stdout)
        report = RESULTS / 'geometry-lease.txt'
        wine = RESULTS / 'geometry-lease-wine.log'
        with report.open('wb') as output, wine.open('wb') as error:
            run = subprocess.run(command, cwd=ROOT, stdout=output, stderr=error,
                                 env=dict(os.environ, X3M_ADMISSION='0', WINEDLLOVERRIDES='d3d9=b'), timeout=120)
        text = report.read_text()
        terminal = re.findall(r'^RESULT PASS checks=(\d+)\s*$', text, re.M)
        count = sum(line.startswith('CHECK ') and line.endswith(' PASS') for line in text.splitlines())
        meta.update(exit_code=run.returncode, checks=count, source_hashes_after=hashes(),
                    report_sha256=digest(report), wine_log_sha256=digest(wine),
                    binary_unchanged=digest(EXE) == meta['executable_sha256'])
        if sum(line.startswith('RESULT ') for line in text.splitlines()) != 1 or len(terminal) != 1 or int(terminal[0]) != count or not text.rstrip().endswith('RESULT PASS checks=' + str(count)):
            raise RuntimeError('Incomplete or duplicate terminal result')
        if count != 421 or 'FAIL' in text or run.returncode != 0:
            raise RuntimeError('Fixture failed')
        if before != meta['source_hashes_after'] or not meta['binary_unchanged']:
            raise RuntimeError('Sources or executable changed during execution')
        meta.update(passed=True, phase='complete', limits=[
            'Synthetic original D3D9 on the recorded Preview runtime; portable API source is not native-Windows runtime verification.',
            'Public leases hold native references only; no production replay/capture hook is enabled by this fixture.',
            'Leases preserve allocation lifetime, not contents; replay must revalidate immutable evidence requests and obey serialized native access.',
        ])
    except (Exception, KeyboardInterrupt) as error:
        meta.update(passed=False, phase='failed', error=repr(error))
    save()
    print(json.dumps({key: meta.get(key) for key in ['passed', 'phase', 'checks', 'error', 'executable_sha256']}, indent=2))
    return 0 if meta['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
