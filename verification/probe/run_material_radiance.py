#!/usr/bin/env python3
"""Freshly build and execute original material-radiance verification in Preview."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory


def main():
    root = Path(__file__).resolve().parents[2]
    executable = root / 'verification/probe/build/material_radiance_fixture.exe'
    results = bottle.results_dir(root)
    files = ('verification/probe/material_radiance_fixture.cpp',
             'verification/probe/build_material_radiance.sh', 'verification/probe/run_material_radiance.py',
             'src/renderer/material_radiance.cpp', 'src/renderer/material_radiance.h',
             'src/renderer/material_radiance_profiles_inc.h')
    def hashes():
        return {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in files}
    before = hashes()
    report = dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(), bottle=bottle.describe(),
                  source_sha256=before, process_local_override='d3d9=b', timeout_seconds=60)
    build = subprocess.run(['sh', str(root / 'verification/probe/build_material_radiance.sh')],
                           cwd=root, timeout=60)
    report.update(build_exit_code=build.returncode, fresh_build=True)
    if build.returncode or hashes() != before:
        report.update(passed=False, reason='Build failed or source changed during compilation')
    else:
        command = ['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine',
                   '--bottle', bottle.BOTTLE, '--no-update', '--workdir', str(executable.parent),
                   str(executable), r'C:\X3\d3dx9_37.dll']
        report.update(command=command, executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest())
        env = os.environ.copy()
        env['WINEDLLOVERRIDES'] = 'd3d9=b'
        with (results / 'material-radiance.txt').open('w') as out, (results / 'material-radiance-wine.log').open('w') as err:
            try:
                process = subprocess.run(command, stdout=out, stderr=err, env=env, timeout=60)
                report['exit_code'] = process.returncode
            except subprocess.TimeoutExpired:
                report.update(exit_code=None, timed_out=True)
        text = (results / 'material-radiance.txt').read_text()
        checks = [line for line in text.splitlines() if line.startswith('CHECK ')]
        samples = [line for line in text.splitlines() if line.startswith('SAMPLE ')]
        keys = re.findall(r'^SAMPLE generation=(\d+) variant=(\d+) branch=(\d+) input=([-\d.]+) alpha=([-\d.]+) target=(\d+)', text, re.M)
        expected = {(str(g), str(v), str(b), value, alpha, str(t))
                    for g in range(2) for v in range(2) for b in range(2)
                    for value in ('-2.000', '0.000', '0.250', '1.000', '4.000', '16.000')
                    for alpha in ('0.250', '1.500') for t in range(2)}
        report.update(checks=len(checks), samples=len(samples), reset_passed='RESET PASS' in text,
                      failed_checks=[line for line in checks + samples if not line.endswith(' PASS')],
                      source_unchanged_during_run=hashes() == before,
                      executable_unchanged_during_run=hashlib.sha256(executable.read_bytes()).hexdigest() == report['executable_sha256'])
        report['passed'] = (report['exit_code'] == 0 and not report['failed_checks'] and len(checks) == 42
                            and len(samples) == 192 and len(keys) == 192 and set(keys) == expected
                            and report['reset_passed'] and report['source_unchanged_during_run']
                            and report['executable_unchanged_during_run'] and 'RESULT PASS ' in text)
    (results / 'material-radiance-summary.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
