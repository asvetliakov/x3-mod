#!/usr/bin/env python3
"""Build current detached rigid producer; original GPU inputs, no game launch."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess

root = Path(__file__).resolve().parents[2]
results = root / 'verification/results'
exe = root / 'verification/probe/build/rigid_motion_fixture.exe'
production_object = root / 'verification/probe/build/rigid_motion_production_contract.o'
paths = [root / name for name in (
    'src/renderer/rigid_motion.h', 'src/renderer/rigid_motion.cpp',
    'src/renderer/rigid_replay_program.h', 'src/renderer/rigid_replay_program.cpp',
    'src/renderer/rigid_position.h', 'src/renderer/rigid_position.cpp',
    'src/renderer/rigid_position_profiles_inc.h', 'src/renderer/position_path_profiles_inc.h',
    'src/renderer/pixel_coverage_profiles_inc.h', 'src/temporal/rigid_motion_ps.hlsl',
    'CMakeLists.txt',
    'src/temporal/resolve.h', 'src/temporal/resolve.hlsl',
    'verification/probe/rigid_motion_fixture.cpp',
    'verification/probe/build_rigid_motion.sh', 'verification/probe/run_rigid_motion.py')]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def hashes():
    return {str(path.relative_to(root)): sha(path) for path in paths}

report = {'passed': False, 'sources_before_build': hashes(), 'game_launched': False,
          'cpu_baseline': 'SSE2; stack realignment; four-byte incoming Win32 stack',
          'verification_build_define': 'X3M_RIGID_MOTION_VERIFICATION=1; synthetic issuer absent from production builds',
          'scope': 'Original native pure-device GPU fixture using production rigid producer and resolve; no live routing'}
try:
    subprocess.run(['sh', str(root / 'verification/probe/build_rigid_motion.sh')], check=True, cwd=root)
    assert hashes() == report['sources_before_build'], 'Source changed during build'
    report['executable_sha256'] = sha(exe)
    report['production_object_sha256'] = sha(production_object)
    symbols = subprocess.run(['i686-w64-mingw32-nm', '-C', str(production_object)], check=True, capture_output=True, text=True).stdout
    assert 'qualify_rigid_replay_source' in symbols and 'original_synthetic_sm3_contract' not in symbols
    report['production_has_synthetic_issuer'] = False
    d3dx = Path.home() / 'Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/d3dx9_37.dll'
    report['d3dx9_37_sha256'] = sha(d3dx)
    command = ['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine',
               '--bottle', 'Steam', '--no-update', '--dll', 'd3d9=b', '--workdir', str(exe.parent),
               str(exe), r'C:\X3\d3dx9_37.dll']
    command += ['Z:' + str(root / 'src/temporal' / name)
                for name in ('rigid_motion_ps.hlsl', 'resolve.hlsl')]
    proof = root / 'verification/results/archive-position-paths.json'
    report['archive_proof_sha256'] = sha(proof)
    programs = [p for p in json.loads(proof.read_text())['programs'] if p['category'] == 'homogeneous_row_dots']
    selected = [next(p for p in programs if p['model'] == '3_0'), next(p for p in programs if p['model'] != '3_0')]
    raw = {Path('/tmp/x3-shader-sweep/programs') / (p['id'] + '.bin'): p['proof']['sha256'] for p in selected}
    assert all(sha(path) == expected for path, expected in raw.items())
    report['local_qualification_inputs'] = {path.name: expected for path, expected in raw.items()}
    command += ['Z:' + str(path) for path in raw]
    report['command'] = command
    active = subprocess.run(['pgrep', '-if', '[X]3AP.exe'], capture_output=True, text=True)
    assert active.returncode == 1, 'X3AP running; postpone synthetic GPU verification'
    with (results / 'rigid-motion.txt').open('w') as out, (results / 'rigid-motion-wine.log').open('w') as err:
        run = subprocess.run(command, stdout=out, stderr=err,
                             env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'), timeout=90)
    report['exit_code'] = run.returncode
    text = (results / 'rigid-motion.txt').read_text()
    report['source_unchanged'] = hashes() == report['sources_before_build']
    report['binary_unchanged'] = sha(exe) == report['executable_sha256'] and sha(production_object) == report['production_object_sha256']
    report['compiler_unchanged'] = sha(d3dx) == report['d3dx9_37_sha256']
    report['qualification_inputs_unchanged'] = sha(proof) == report['archive_proof_sha256'] and all(sha(path) == expected for path, expected in raw.items())
    report['report_sha256'] = sha(results / 'rigid-motion.txt')
    samples = re.findall(r'^SAMPLE .* actual=([-\d.]+) expected=([-\d.]+) PASS$', text, re.M)
    report['numerical_samples'] = len(samples)
    report['maximum_absolute_numeric_error'] = max(abs(float(a) - float(b)) for a, b in samples)
    report['large_viewport_errors'] = [dict(zip(('width', 'height', 'x', 'y', 'uv_x', 'uv_y', 'pixel_x', 'pixel_y', 'depth'), [float(value) for value in row])) for row in re.findall(r'^PRECISION_ERROR width=(\d+) height=(\d+) x=(\d+) y=(\d+) uv_x=([\d.]+) uv_y=([\d.]+) pixel_x=([\d.]+) pixel_y=([\d.]+) depth=([\d.]+)$', text, re.M)]
    report['checks'] = sum(line.startswith('CHECK ') and line.endswith(' PASS') for line in text.splitlines())
    match = re.search(r'RESULT PASS numerical=(\d+) checks=(\d+) state_restorations=(\d+) generations=(\d+)', text)
    report['state_restorations'] = int(match[3]) if match else 0
    report['generations'] = int(match[4]) if match else 0
    assert run.returncode == 0 and match and tuple(map(int, match.groups())) == (102, 117, 30, 2), text[-1500:]
    assert report['numerical_samples'] == 102 and report['checks'] == 117 and 'RESET PASS' in text and 'FAIL' not in text
    assert report['source_unchanged'] and report['binary_unchanged'] and report['compiler_unchanged'] and report['qualification_inputs_unchanged'], 'Provenance changed'
    report['passed'] = True
finally:
    (results / 'rigid-motion-summary.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
