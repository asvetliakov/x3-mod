#!/usr/bin/env python3
"""Fresh-build native bounded retirement verification; never launches X3."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
RESULTS = ROOT / 'verification/results'
EXE = ROOT / 'verification/probe/build/rigid_retirement_fixture.exe'
SOURCE_NAMES = (
    'src/renderer/rigid_motion.h', 'src/renderer/rigid_motion.cpp',
    'src/renderer/rigid_replay_program.h', 'src/renderer/rigid_replay_program.cpp',
    'src/renderer/rigid_position.h', 'src/renderer/rigid_position.cpp',
    'src/renderer/rigid_position_profiles_inc.h', 'src/renderer/position_path_profiles_inc.h',
    'src/renderer/pixel_coverage_profiles_inc.h', 'src/renderer/rigid_motion_pixel_program.h',
    'src/renderer/rigid_motion_pixel_program_inc.h',
    'verification/probe/rigid_retirement_fixture.cpp',
    'verification/probe/build_rigid_retirement.sh', 'verification/probe/run_rigid_retirement.py')

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def sources():
    return {name: sha(ROOT / name) for name in SOURCE_NAMES}

def validate_report(text):
    lines = text.splitlines()
    expected = 'RESULT PASS checks=168 cases=10 callbacks=84 devices=2'
    assert lines and lines[-1] == expected
    assert [line for line in lines if line.startswith('RESULT ')] == [expected]
    assert 'FAIL' not in text
    check_lines = [line for line in lines if line.startswith('CHECK ')]
    assert len(check_lines) == 168 and all(line.endswith(' PASS') for line in check_lines)
    assert [line for line in lines if line.startswith('DEVICE ')] == ['DEVICE pure=0', 'DEVICE pure=1']
    assert lines.count('RESET PASS') == 2
    for name, count in (
        ('batch initially reusable', 10), ('native injection reached', 10),
        ('partial capture did not acquire failed depth output', 2),
        ('explicit release retires seven witnesses and refuses callback reentry', 10),
        ('each private IUnknown witness destroyed exactly once', 70),
        ('destructor retires outside caller scope', 2), ('native device final release', 2)):
        assert lines.count('CHECK ' + name + ' PASS') == count
    return {'checks': 168, 'cases': 10, 'callbacks': 84, 'devices': 2, 'resets': 2}

def main():
    summary_path = RESULTS / 'rigid-retirement-summary.json'
    report_path = RESULTS / 'rigid-retirement.txt'
    report = {'passed': False, 'status': 'RUNNING', 'game_launched': False,
              'scope': 'Detached native callback retirement; simulated failure injection; no live capture wiring or callback-free guarantee',
              'cpu_baseline': 'SSE2; stack realignment; four-byte incoming Win32 stack'}
    summary_path.write_text(json.dumps(report, indent=2) + '\n')
    try:
        active = subprocess.run(['pgrep', '-ifl', '[X]3AP[.]exe'], capture_output=True, text=True)
        assert active.returncode == 1 and not active.stdout.strip(), 'Game running'
        report['sources_before_build'] = sources()
        subprocess.run(['sh', str(ROOT / 'verification/probe/build_rigid_retirement.sh')], cwd=ROOT, check=True)
        report['sources_after_build'] = sources()
        assert report['sources_before_build'] == report['sources_after_build']
        report['executable_sha256'] = sha(EXE)
        wine = '/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine'
        report['wine_sha256'] = sha(Path(wine))
        command = [wine, '--bottle', 'Steam', '--no-update', '--dll', 'd3d9=b', str(EXE)]
        report['command'] = command
        active = subprocess.run(['pgrep', '-ifl', '[X]3AP[.]exe'], capture_output=True, text=True)
        assert active.returncode == 1 and not active.stdout.strip(), 'Game running'
        with report_path.open('w') as out, (RESULTS / 'rigid-retirement-wine.log').open('w') as err:
            run = subprocess.run(command, stdout=out, stderr=err, timeout=60,
                                 env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'))
        report['exit_code'] = run.returncode
        assert run.returncode == 0
        report.update(validate_report(report_path.read_text()))
        report['sources_after_run'] = sources()
        assert report['sources_after_run'] == report['sources_before_build']
        assert sha(EXE) == report['executable_sha256'] and sha(Path(wine)) == report['wine_sha256']
        report['report_sha256'] = sha(report_path)
        report['passed'] = True
        report['status'] = 'PASS'
    except BaseException as error:
        report['status'] = 'FAIL'
        report['error'] = repr(error)
        raise
    finally:
        summary_path.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
