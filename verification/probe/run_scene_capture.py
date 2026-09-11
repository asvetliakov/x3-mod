#!/usr/bin/env python3
"""Freshly build/run original SceneCapture adapter integration, without X3."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def main():
    root = Path(__file__).resolve().parents[2]
    executable = root / 'verification/probe/build/scene_capture_fixture.exe'
    results = root / 'verification/results'
    names = ('positive', 'unsupported', 'failed-color-copy', 'failed-final-clear',
             'rejected-depth-copy', 'generation-mismatch', 'failed-draw-after-selection',
             'failed-present-after-selection', 'inactive-capture', 'post-clear-binding-query-failure', 'scratch-color-fills',
             'main-color-fill', 'depth-color-fill', 'unknown-color-fill', 'failed-color-fill',
             'wrong-phase-color-fill', 'standalone-color-fill', 'first-rejection-preserved')
    files = ('verification/probe/scene_capture_fixture.cpp',
             'verification/probe/build_scene_capture.sh', 'verification/probe/run_scene_capture.py',
             'src/ownership/d3d9_ownership.cpp', 'src/ownership/execution_state.cpp', 'src/ownership/execution_state.h', 'src/ownership/finite_buffer_evidence.cpp', 'src/ownership/finite_buffer_evidence.h', 'src/ownership/portable_managed_upload.cpp', 'src/ownership/portable_managed_upload.h', 'src/ownership/d3d9_ownership.h',
             'src/ownership/d3d9_classes_inc.h', 'src/ownership/d3d9_forwarders_inc.h',
             'src/proxy/scene_capture.cpp', 'src/proxy/scene_capture.h',
             'src/proxy/capture_state.cpp', 'src/proxy/capture_state.h', 'src/proxy/capture.h',
             'src/renderer/scene_boundary.h', 'src/temporal/depth_decode.hlsl')
    def hashes():
        return {f: hashlib.sha256((root / f).read_bytes()).hexdigest() for f in files}
    before = hashes()
    report = dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  source_sha256=before, process_local_override='d3d9=b', timeout_seconds=90)
    build = subprocess.run(['sh', str(root / 'verification/probe/build_scene_capture.sh')],
                           cwd=root, timeout=60)
    report.update(build_exit_code=build.returncode, fresh_build=True)
    if build.returncode or hashes() != before:
        report.update(passed=False, reason='Build failed or source changed during compilation')
    else:
        command = ['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine',
                   '--bottle', 'Steam', '--no-update', '--workdir', str(executable.parent),
                   str(executable), r'C:\X3\d3dx9_37.dll', 'Z:' + str(root / 'src/temporal/depth_decode.hlsl')]
        report.update(command=command, executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest())
        env = os.environ.copy()
        env['WINEDLLOVERRIDES'] = 'd3d9=b'
        with (results / 'scene-capture.txt').open('w') as out, (results / 'scene-capture-wine.log').open('w') as err:
            try:
                process = subprocess.run(command, stdout=out, stderr=err, env=env, timeout=90)
                report['exit_code'] = process.returncode
            except subprocess.TimeoutExpired:
                report.update(exit_code=None, timed_out=True)
        text = (results / 'scene-capture.txt').read_text()
        checks = [line for line in text.splitlines() if line.startswith('CHECK ')]
        samples = [line for line in text.splitlines() if line.startswith('SAMPLE ')]
        scenarios = re.findall(r'^SCENARIO (\S+) flags=(\S+) .* PASS$', text, re.M)
        expected = {(name, flags) for name in names for flags in ('00000040', '00000050')}
        report.update(checks=len(checks), samples=len(samples), scenarios=len(scenarios),
                      failed_checks=[line for line in checks + samples if not line.endswith(' PASS')],
                      source_unchanged_during_run=hashes() == before,
                      executable_unchanged_during_run=hashlib.sha256(executable.read_bytes()).hexdigest() == report['executable_sha256'])
        report['passed'] = (report['exit_code'] == 0 and not report['failed_checks']
                            and len(samples) == 16 and len(scenarios) == 36 and set(scenarios) == expected
                            and report['source_unchanged_during_run'] and report['executable_unchanged_during_run']
                            and 'RESULT PASS ' in text)
    (results / 'scene-capture-summary.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
