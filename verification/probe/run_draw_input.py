#!/usr/bin/env python3
"""Build and verify live input acquisition on original wrapped Win32 D3D9 draws."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
DLL = Path.home() / 'Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/d3dx9_37.dll'
SYSTEM = DLL.parent.parent / 'windows/syswow64'
BACKENDS = {
    'native_d3d9.dll': SYSTEM / 'd3d9.dll',
    'native_wined3d.dll': SYSTEM / 'wined3d.dll',
}
FILES = [
    'src/proxy/draw_input.h', 'src/proxy/draw_input.cpp',
    'src/proxy/capture_state.h', 'src/proxy/capture_state.cpp', 'src/proxy/capture.h',
    'src/proxy/object_trace.h', 'src/renderer/motion_history.h',
    'src/renderer/rigid_position.h', 'src/renderer/rigid_position.cpp',
    'src/renderer/rigid_motion.h', 'src/renderer/rigid_motion.cpp',
    'src/renderer/rigid_replay_program.h', 'src/renderer/rigid_replay_program.cpp',
    'src/renderer/rigid_position_profiles_inc.h', 'src/renderer/position_path_profiles_inc.h',
    'src/renderer/pixel_coverage_profiles_inc.h',
    'src/ownership/d3d9_ownership.h', 'src/ownership/d3d9_ownership.cpp',
    'src/ownership/execution_state.h', 'src/ownership/execution_state.cpp', 'src/ownership/finite_buffer_evidence.cpp', 'src/ownership/finite_buffer_evidence.h', 'src/ownership/portable_managed_upload.cpp', 'src/ownership/portable_managed_upload.h',
    'src/ownership/d3d9_classes_inc.h', 'src/ownership/d3d9_forwarders_inc.h',
    'verification/probe/draw_input_fixture.cpp', 'verification/probe/build_draw_input.sh',
    'verification/probe/run_draw_input.py',
]
EXE = ROOT / 'verification/probe/build/draw_input_fixture.exe'
ARCHIVE = Path('/tmp/x3-shader-sweep/programs/vs_b0602757fce6e870.bin')
ARCHIVE_SHA256 = '33cef191db2668aadef869a140b2185b1576212535bdae7d607da49573d8d785'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def hashes():
    return ({name: digest(ROOT / name) for name in FILES} | {'native_d3dx9_37.dll': digest(DLL), 'local_archive_vs_b0602757fce6e870.bin': digest(ARCHIVE)} |
            {name: digest(path) for name, path in BACKENDS.items()})


def main():
    results = ROOT / 'verification/results'
    summary = results / 'draw-input-summary.json'
    meta = dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                passed=False, phase='building', fresh_build=False, game_launched=False)

    def save():
        summary.write_text(json.dumps(meta, indent=2) + '\n')

    save()  # A failed prebuild must not leave an older PASS visible.
    try:
        before = hashes()
        meta['source_hashes_before_build'] = before
        save()
        if before['local_archive_vs_b0602757fce6e870.bin'] != ARCHIVE_SHA256:
            raise RuntimeError('Local reviewed archive VS differs from expected input')
        subprocess.run(['sh', 'verification/probe/build_draw_input.sh'], cwd=ROOT,
                       check=True, timeout=60)
        meta['source_hashes_after_build'] = hashes()
        if before != meta['source_hashes_after_build']:
            raise RuntimeError('Sources changed during build')
        meta['executable_sha256'] = digest(EXE)
        command = ['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine',
                   '--bottle', 'Steam', '--no-update', '--dll', 'd3d9=b',
                   '--workdir', str(EXE.parent), str(EXE), r'C:\X3\d3dx9_37.dll', 'Z:' + str(ARCHIVE)]
        meta.update(fresh_build=True, phase='running', command=command)
        save()
        active = subprocess.run(['pgrep', '-ifl', '[X]3AP[.]exe'], capture_output=True, text=True)
        if active.returncode != 1 or active.stdout.strip():
            raise RuntimeError('Game present or process inventory failed; postpone native verification: ' + active.stdout)
        meta['no_game_before_run'] = True
        report = results / 'draw-input.txt'
        wine = results / 'draw-input-wine.log'
        with report.open('wb') as out, wine.open('wb') as err:
            run = subprocess.run(command, stdout=out, stderr=err, timeout=90,
                                 env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'))
        text = report.read_text()
        meta.update(exit_code=run.returncode, source_hashes_after=hashes(),
                    report_sha256=digest(report), binary_unchanged=digest(EXE) == meta['executable_sha256'])
        terminal = text.rstrip().splitlines()[-1] if text.strip() else ''
        result_lines = [line for line in text.splitlines() if line.startswith('RESULT ')]
        match = (re.fullmatch(r'RESULT PASS checks=(\d+) reads=(\d+) state_checks=(\d+)', terminal)
                 if result_lines == [terminal] else None)
        checks = sum(line.startswith('CHECK ') and line.endswith(' PASS') for line in text.splitlines())
        if not match or tuple(map(int, match.groups())) != (260, 74, 74) or checks != 260:
            raise RuntimeError('Unexpected or incomplete fixture report')
        meta.update(checks=checks, draw_reads=74, caller_state_snapshots=74, getter_fault_cases=7)
        meta['passed'] = (run.returncode == 0 and 'FAIL' not in text and
                          before == meta['source_hashes_after'] and meta['binary_unchanged'])
        meta['phase'] = 'complete'
        meta['limits'] = [
            'Original shaders exercise fixture callbacks; a pinned local archive VS is bound but never executed to verify production source-qualified lease admission.',
            'No game launched or modified; no live replay, jitter or TAA enabled.',
            'Finite attestation is opt-in upload evidence for the exact revision/range; it does not prove engine lifetimes, final color ownership, or stability through later replay.',
            'State snapshots cover all reader-consumed device state plus surrounding constants, scissor and sampler state; not every D3D9 state slot.',
        ]
    except (Exception, KeyboardInterrupt) as error:
        meta.update(passed=False, phase='failed', error=repr(error))
    save()
    print(json.dumps(meta, indent=2))
    return 0 if meta['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
