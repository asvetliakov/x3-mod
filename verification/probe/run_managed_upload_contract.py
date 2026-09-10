#!/usr/bin/env python3
"""Fresh-build exact native managed-upload qualification. No game or installation."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
FILES = ['src/ownership/managed_upload_contract.h', 'src/ownership/managed_upload_contract.cpp',
         'verification/probe/managed_upload_contract_fixture.cpp',
         'verification/probe/build_managed_upload_contract.sh',
         'verification/probe/run_managed_upload_contract.py']
EXE = ROOT / 'verification/probe/build/managed_upload_contract_fixture.exe'
RESULTS = ROOT / 'verification/results'
NATIVE = Path.home() / 'Library/Application Support/CrossOver/Bottles/Steam/drive_c/windows/syswow64'
EXPECTED_NATIVE = {
    'd3d9.dll': '58cc36cf74128ae4b6211100430d146c3692808146d8d2075e6c5d846162f8cf',
    'wined3d.dll': 'f4997bc0465de7e87bac9921bf0274db00ac3b3ba0754fa03f1f33e309a8e863',
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def hashes():
    return ({name: digest(ROOT / name) for name in FILES}
            | {'native/' + name: digest(NATIVE / name) for name in EXPECTED_NATIVE})


def main():
    metadata = dict(passed=False, game_launched=False, fresh_build=False,
                    started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    summary = RESULTS / 'managed-upload-contract-summary.json'
    summary.write_text(json.dumps(metadata, indent=2) + '\n')
    try:
        processes = subprocess.run(['pgrep', '-ifl', 'X3AP.exe'], capture_output=True, text=True)
        if processes.returncode not in (0, 1) or processes.stdout.strip():
            raise RuntimeError('Game process present or process inventory failed: ' + processes.stdout)
        before = hashes()
        metadata['sources_before'] = before
        if any(before['native/' + name] != expected for name, expected in EXPECTED_NATIVE.items()):
            raise RuntimeError('Native runtime files differ from the pinned contract')
        build = subprocess.run(['sh', 'verification/probe/build_managed_upload_contract.sh'],
                               cwd=ROOT, capture_output=True, text=True, timeout=60)
        (RESULTS / 'managed-upload-contract-build.txt').write_text(build.stdout + build.stderr)
        metadata['build_exit'] = build.returncode
        metadata['sources_after_build'] = hashes()
        if build.returncode or before != metadata['sources_after_build']:
            raise RuntimeError('Build failed or sources changed during compilation')
        metadata.update(fresh_build=True, executable_sha256=digest(EXE))
        command = ['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine',
                   '--bottle', 'Steam', '--no-update', '--workdir', str(EXE.parent), str(EXE)]
        metadata['command'] = command
        metadata['process_local_override'] = 'd3d9=b'
        report = RESULTS / 'managed-upload-contract.txt'
        with report.open('wb') as out, (RESULTS / 'managed-upload-contract-wine.log').open('wb') as err:
            run = subprocess.run(command, stdout=out, stderr=err, timeout=90,
                                 env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'))
        text = report.read_text()
        metadata.update(exit_code=run.returncode, sources_after=hashes(),
                        executable_sha256_after=digest(EXE), report_sha256=digest(report))
        checks = sum(line.startswith('CHECK ') and line.endswith(' PASS') for line in text.splitlines())
        lines = [line for line in text.splitlines() if line.strip()]
        result_lines = [line for line in lines if line.startswith('RESULT')]
        match = re.fullmatch(r'RESULT PASS checks=(\d+)', lines[-1]) if lines else None
        metadata.update(checks=checks, cases=[line for line in lines if line.startswith('CASE ')])
        expected_cases = ['CASE buffer kind=vertex format=101',
                          'CASE buffer kind=index format=101', 'CASE buffer kind=index format=102']
        metadata['passed'] = (run.returncode == 0 and match is not None and int(match[1]) == checks == 461
                              and result_lines == [lines[-1]] and metadata['cases'] == expected_cases and 'FAIL' not in text
                              and before == metadata['sources_after']
                              and metadata['executable_sha256'] == metadata['executable_sha256_after'])
    except (Exception, KeyboardInterrupt) as error:
        metadata.update(passed=False, error=repr(error))
    summary.write_text(json.dumps(metadata, indent=2) + '\n')
    print(json.dumps({k: v for k, v in metadata.items() if not k.startswith('sources_')}, indent=2))
    return 0 if metadata['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
