#!/usr/bin/env python3
"""Run bounded production-wrapper depth verification in Preview, without X3."""
import datetime
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess


def compatibility_report(text):
    records = [dict(re.findall(r'(\w+)=([^\s]+)', line)) for line in text.splitlines()
               if line.startswith('COMPATIBILITY ')]
    return dict(observations=records,
                depth_copy_parity='DEPTH_COPY_PARITY matched=1' in text,
                gameplay_ready=False,
                remaining_gates=[
                    'INTZ/D24X8 depth copies differ from original D24X8/D24X8 copies.',
                    'Scoped stencil masking during application BeginStateBlock recording is not equivalent.',
                    'Real device loss, multithreaded callers, and gameplay remain unverified.'])


def main():
    root = Path(__file__).resolve().parents[2]
    executable = root / 'verification/probe/build/auto_depth_fixture.exe'
    results = root / 'verification/results'
    command = ['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine',
               '--bottle', 'Steam', '--no-update', '--workdir', str(executable.parent),
               str(executable), r'C:\X3\d3dx9_37.dll']
    env = os.environ.copy()
    env['WINEDLLOVERRIDES'] = 'd3d9=b'
    files = ['verification/probe/auto_depth_fixture.cpp', 'src/ownership/d3d9_ownership.cpp',
             'src/ownership/d3d9_ownership.h', 'src/ownership/d3d9_classes_inc.h',
             'src/ownership/d3d9_forwarders_inc.h']
    report = dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  command=command, timeout_seconds=60, process_local_override='d3d9=b',
                  source_sha256={f: hashlib.sha256((root/f).read_bytes()).hexdigest() for f in files},
                  executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest())
    with (results/'auto-depth.txt').open('w') as out, (results/'auto-depth-wine.log').open('w') as err:
        try:
            run = subprocess.run(command, env=env, stdout=out, stderr=err, timeout=60)
            report['exit_code'] = run.returncode
        except subprocess.TimeoutExpired:
            report.update(exit_code=None, timed_out=True)
    text = (results/'auto-depth.txt').read_text()
    checks = [line for line in text.splitlines() if line.startswith('CHECK ')]
    samples = [line for line in text.splitlines() if line.startswith('SAMPLE ')]
    report.update(checks=len(checks), samples=len(samples),
                  failed_checks=[line for line in checks+samples if not line.endswith(' PASS')])
    report['compatibility'] = compatibility_report(text)
    report['passed'] = (report['exit_code'] == 0 and len(samples) == 48
                        and not report['failed_checks'] and 'RESULT PASS ' in text)
    (results/'auto-depth-summary.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
