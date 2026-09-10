#!/usr/bin/env python3
"""Fresh-build private MotionCapture diagnostics; original scene, never the game."""
from pathlib import Path
import hashlib
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[2]
RESULTS = ROOT / 'verification/results'
EXE = ROOT / 'verification/probe/build/motion_capture_fixture.exe'
BOTTLE = Path.home() / 'Library/Application Support/CrossOver/Bottles/Steam/drive_c'
NATIVE = {'native_d3dx9_37.dll': BOTTLE / 'X3/d3dx9_37.dll',
          'native_d3d9.dll': BOTTLE / 'windows/syswow64/d3d9.dll',
          'native_wined3d.dll': BOTTLE / 'windows/syswow64/wined3d.dll'}
FILES = [
    'src/proxy/motion_capture.h', 'src/proxy/motion_capture.cpp',
    'src/proxy/draw_input.h', 'src/proxy/draw_input.cpp',
    'src/proxy/object_lifetime.h', 'src/proxy/object_trace.h',
    'src/proxy/cpu_state.h', 'src/proxy/capture_state.h', 'src/proxy/capture_state.cpp', 'src/proxy/capture.h',
    'src/ownership/d3d9_ownership.h', 'src/ownership/d3d9_ownership.cpp',
    'src/ownership/d3d9_classes_inc.h', 'src/ownership/d3d9_forwarders_inc.h',
    'src/ownership/finite_buffer_evidence.h', 'src/ownership/finite_buffer_evidence.cpp',
    'src/ownership/managed_upload_contract.h', 'src/ownership/managed_upload_contract.cpp',
    'src/ownership/execution_state.h', 'src/ownership/execution_state.cpp',
    'src/renderer/motion_history.h', 'src/renderer/motion_history.cpp',
    'src/renderer/scene_boundary.h', 'src/renderer/rigid_motion.h', 'src/renderer/rigid_motion.cpp',
    'src/renderer/rigid_replay_program.h', 'src/renderer/rigid_replay_program.cpp',
    'src/renderer/rigid_motion_pixel_program.h', 'src/renderer/rigid_motion_pixel_program_inc.h',
    'src/renderer/rigid_position.h', 'src/renderer/rigid_position.cpp',
    'src/renderer/rigid_position_profiles_inc.h', 'src/renderer/position_path_profiles_inc.h',
    'src/renderer/pixel_coverage_profiles_inc.h',
    'verification/probe/motion_capture_fixture.cpp', 'verification/probe/build_motion_capture.sh',
    'verification/probe/run_motion_capture.py']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def hashes():
    return {name: sha(ROOT / name) for name in FILES} | {name: sha(path) for name, path in NATIVE.items()}


def validate_report(text):
    lines = text.splitlines()
    terminal = text.rstrip().splitlines()[-1] if text.strip() else ''
    endings = [line for line in lines if line.startswith('RESULT ')]
    expected = 'RESULT PASS checks=253 samples=40 state_comparisons=28 devices=2'
    if endings != [expected] or terminal != expected or 'FAIL' in text:
        raise RuntimeError('Missing, duplicate, incomplete or nonterminal fixture result')
    checks = [line for line in lines if line.startswith('CHECK ')]
    samples = [line for line in lines if line.startswith('SAMPLE ')]
    if len(checks) != 253 or not all(line.endswith(' PASS') for line in checks):
        raise RuntimeError('Unexpected check inventory')
    if len(samples) != 40 or not all(line.endswith(' PASS') for line in samples):
        raise RuntimeError('Unexpected numeric inventory')
    if lines.count('CHECK pre-Clear replay preserves captured application state PASS') != 28:
        raise RuntimeError('Unexpected native-state comparison inventory')
    if lines.count('CHECK boundary preserves full x87 MXCSR and LastError after injected work PASS') != 28:
        raise RuntimeError('Unexpected CPU-state preservation inventory')
    if [line for line in lines if line.startswith('DEVICE ')] != ['DEVICE pure=0', 'DEVICE pure=1']:
        raise RuntimeError('Both normal and pure devices must complete')
    return dict(checks=253, samples=40, state_comparisons=28, cpu_state_comparisons=28, devices=2)


def main():
    path = RESULTS / 'motion-capture-summary.json'
    report = dict(passed=False, phase='building', game_launched=False, fresh_build=False,
                  scope='Private diagnostic: FLOAT3 nonindexed; supplied Selection/Clear confirmation/Present results; synthetic source/lifetime; excludes capture.cpp and SceneCapture orchestration; no TAA eligibility')

    def save():
        path.write_text(json.dumps(report, indent=2) + '\n')

    save()
    try:
        before = hashes()
        report['sources_before_build'] = before
        save()
        subprocess.run(['sh', str(ROOT / 'verification/probe/build_motion_capture.sh')], check=True, cwd=ROOT, timeout=90)
        report['sources_after_build'] = hashes()
        if before != report['sources_after_build']:
            raise RuntimeError('Source changed during compilation')
        report.update(fresh_build=True, executable_sha256=sha(EXE), phase='running')
        active = subprocess.run(['pgrep', '-ifl', '[X]3AP[.]exe'], capture_output=True, text=True)
        if active.returncode != 1 or active.stdout.strip():
            raise RuntimeError('Game present or process inventory unavailable; postpone fixture: ' + active.stdout)
        report['no_game_before_run'] = True
        command = ['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine',
                   '--bottle', 'Steam', '--no-update', '--dll', 'd3d9=b', '--workdir', str(EXE.parent),
                   str(EXE), r'C:\X3\d3dx9_37.dll']
        report['command'] = command
        save()
        output = RESULTS / 'motion-capture.txt'
        with output.open('w') as out, (RESULTS / 'motion-capture-wine.log').open('w') as err:
            run = subprocess.run(command, stdout=out, stderr=err, timeout=90,
                                 env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'))
        text = output.read_text()
        report.update(exit_code=run.returncode, sources_after_run=hashes(),
                      executable_unchanged=sha(EXE) == report['executable_sha256'], report_sha256=sha(output))
        if run.returncode:
            raise RuntimeError('Fixture exited unsuccessfully: ' + text[-2000:])
        report.update(validate_report(text))
        if before != report['sources_after_run'] or not report['executable_unchanged']:
            raise RuntimeError('Provenance changed during verification')
        report.update(passed=True, phase='complete', devices=2)
    except (Exception, KeyboardInterrupt) as error:
        report.update(passed=False, phase='failed', error=repr(error))
    save()
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
