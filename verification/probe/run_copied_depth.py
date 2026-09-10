#!/usr/bin/env python3
"""Run bounded production-wrapper depth verification in Preview, without X3."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess



def main():
    root = Path(__file__).resolve().parents[2]
    executable = root / 'verification/probe/build/copied_depth_fixture.exe'
    results = root / 'verification/results'
    command = ['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine',
               '--bottle', 'Steam', '--no-update', '--workdir', str(executable.parent),
               str(executable), r'C:\X3\d3dx9_37.dll', 'Z:' + str(root/'src/temporal/depth_decode.hlsl')]
    env = os.environ.copy()
    env['WINEDLLOVERRIDES'] = 'd3d9=b'
    files = ['verification/probe/copied_depth_fixture.cpp',
             'verification/probe/build_copied_depth.sh', 'verification/probe/run_copied_depth.py',
             'src/ownership/d3d9_ownership.cpp', 'src/ownership/finite_buffer_evidence.cpp', 'src/ownership/finite_buffer_evidence.h', 'src/ownership/managed_upload_contract.cpp', 'src/ownership/managed_upload_contract.h',
             'src/ownership/d3d9_ownership.h', 'src/ownership/d3d9_classes_inc.h',
             'src/ownership/d3d9_forwarders_inc.h', 'src/temporal/depth_decode.hlsl']
    before_build = {f: hashlib.sha256((root/f).read_bytes()).hexdigest() for f in files}
    subprocess.run(['sh', str(root/'verification/probe/build_copied_depth.sh')], cwd=root, check=True, timeout=60)
    if before_build != {f: hashlib.sha256((root/f).read_bytes()).hexdigest() for f in files}:
        raise SystemExit('Source changed during build; rerun after edits finish.')
    report = dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  command=command, timeout_seconds=60, process_local_override='d3d9=b',
                  source_sha256={f: hashlib.sha256((root/f).read_bytes()).hexdigest() for f in files},
                  executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest())
    with (results/'copied-depth.txt').open('w') as out, (results/'copied-depth-wine.log').open('w') as err:
        try:
            run = subprocess.run(command, env=env, stdout=out, stderr=err, timeout=60)
            report['exit_code'] = run.returncode
        except subprocess.TimeoutExpired:
            report.update(exit_code=None, timed_out=True)
    text = (results/'copied-depth.txt').read_text()
    checks = [line for line in text.splitlines() if line.startswith('CHECK ')]
    samples = [line for line in text.splitlines() if line.startswith('SAMPLE ')]
    report.update(checks=len(checks), samples=len(samples),
                  failed_checks=[line for line in checks+samples if not line.endswith(' PASS')])
    report['fresh_build'] = True
    report['source_unchanged_during_run'] = before_build == {f: hashlib.sha256((root/f).read_bytes()).hexdigest() for f in files}
    report['executable_unchanged_during_run'] = report['executable_sha256'] == hashlib.sha256(executable.read_bytes()).hexdigest()
    report['passed'] = (report['source_unchanged_during_run'] and report['executable_unchanged_during_run']
                        and report['exit_code'] == 0 and len(checks) == 634 and len(samples) == 32
                        and not report['failed_checks'] and 'RESULT PASS ' in text)
    (results/'copied-depth-summary.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
