#!/usr/bin/env python3
"""Fresh-build baseline/wrapper ownership fixtures in isolated Preview directories."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
PROBE = ROOT / 'verification/probe/build'
RESULTS = ROOT / 'verification/results'
WINE = '/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_hashes():
    paths = [ROOT / name for name in (
        'verification/probe/ownership_fixture.cpp', 'verification/probe/build_ownership.sh',
        'verification/probe/run_ownership.py', 'verification/probe/verify_ownership.py',
        'tools/ownership/generate_d3d9_forwarders.py')]
    paths.extend(path for path in sorted((ROOT / 'src/ownership').glob('*'))
                 if path.suffix in ('.cpp', '.h'))
    return {str(path.relative_to(ROOT)): digest(path) for path in paths}


def main():
    summary = RESULTS / 'ownership-build-verification.json'
    derived = RESULTS / 'ownership-verification.json'
    derived.write_text(json.dumps(dict(result='RUNNING', reason='Fresh ownership build/run pending')) + '\n')
    manifest = dict(runtime=WINE, bottle='Steam', game_launched=False, fixtures={},
                    passed=False, fresh_build=False, phase='building')

    def save():
        summary.write_text(json.dumps(manifest, indent=2) + '\n')

    save()  # Invalidate old evidence before reading sources or invoking the compiler.
    try:
        before = source_hashes()
        manifest['sources_before_build'] = before
        save()
        subprocess.run(['sh', str(ROOT / 'verification/probe/build_ownership.sh')],
                       cwd=ROOT, check=True, timeout=90)
        manifest['sources_after_build'] = source_hashes()
        if before != manifest['sources_after_build']:
            raise RuntimeError('Ownership inputs changed during compilation')
        binaries = {mode: digest(PROBE / ('ownership_' + mode + '.exe'))
                    for mode in ('baseline', 'wrapped')}
        manifest.update(fresh_build=True, phase='running', binaries_before=binaries)
        save()
        for mode in ('baseline', 'wrapped'):
            exe = 'ownership_' + mode + '.exe'
            directory = PROBE / ('ownership-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '-' + mode)
            directory.mkdir(parents=True)
            shutil.copy(PROBE / exe, directory)
            if digest(directory / exe) != binaries[mode]:
                raise RuntimeError('Copied ownership executable does not match build')
            env = dict(os.environ, WINEDLLOVERRIDES='d3d9=b')
            env.pop('X3M_TELEMETRY', None)
            command = [WINE, '--bottle', 'Steam', '--no-update', '--dll', 'd3d9=b',
                       '--workdir', str(directory), str(directory / exe)]
            stdout_path = RESULTS / f'ownership-{mode}.txt'
            stderr_path = RESULTS / f'ownership-{mode}-wine.log'
            active = subprocess.run(['pgrep', '-ifl', '[X]3AP[.]exe'], capture_output=True, text=True)
            if active.returncode != 1 or active.stdout.strip():
                raise RuntimeError('Game present or process inventory failed; postpone ownership verification: ' + active.stdout)
            with stdout_path.open('w') as stdout, stderr_path.open('w') as stderr:
                completed = subprocess.run(command, env=env, stdout=stdout, stderr=stderr, timeout=90)
            manifest['fixtures'][mode] = dict(exe_sha256=binaries[mode], report_sha256=digest(stdout_path),
                                              exit=completed.returncode, command=command)
            trace = stdout_path.read_text()
            terminal = trace.rstrip().splitlines()[-1] if trace.strip() else ''
            endings = [line for line in trace.splitlines() if line.startswith('OWNERSHIP RESULT ')]
            end = (re.fullmatch(r'OWNERSHIP RESULT checks=(\d+) failures=0', terminal)
                   if endings == [terminal] else None)
            expected_checks = {'baseline': 370, 'wrapped': 431}[mode]
            check_lines = [line for line in trace.splitlines() if line.startswith('CHECK ')]
            valid_end = (end and int(end[1]) == expected_checks == len(check_lines) and
                         all(line.endswith(' PASS') for line in check_lines))
            if completed.returncode or not valid_end or digest(directory / exe) != binaries[mode]:
                raise RuntimeError('Ownership fixture failed or copied executable changed')
        manifest['sources'] = source_hashes()
        manifest['binaries_after'] = {mode: digest(PROBE / ('ownership_' + mode + '.exe'))
                                      for mode in ('baseline', 'wrapped')}
        if manifest['sources'] != before or manifest['binaries_after'] != binaries:
            raise RuntimeError('Ownership inputs changed during verification')
        manifest['source_sha256'] = manifest['sources']['src/ownership/d3d9_ownership.cpp']
        manifest.update(passed=True, phase='complete')
    except (Exception, KeyboardInterrupt) as error:
        manifest.update(passed=False, phase='failed', error=repr(error))
        derived.write_text(json.dumps(dict(result='FAIL', reason=repr(error))) + '\n')
    save()
    print(json.dumps(manifest, indent=2))
    return 0 if manifest['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
