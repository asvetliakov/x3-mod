#!/usr/bin/env python3
"""Fresh-build original replay arithmetic/raster proof; never starts the game."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import tempfile
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_guard import game_running  # noqa: E402
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory

root = Path(__file__).resolve().parents[2]
results = bottle.results_dir(root)
exe = root / 'verification/probe/build/rigid_replay_fixture.exe'
host = root / 'verification/probe/build/rigid_replay_profiles'
paths = [root / name for name in (
    'src/renderer/rigid_replay_program.h', 'src/renderer/rigid_replay_program.cpp',
    'src/renderer/rigid_position.h', 'src/renderer/rigid_position.cpp',
    'src/renderer/rigid_position_profiles_inc.h',
    'src/renderer/position_path_profiles_inc.h',
    'src/renderer/pixel_coverage_profiles_inc.h',
    'verification/probe/rigid_replay_fixture.cpp',
    'verification/probe/rigid_replay_profiles.cpp',
    'verification/probe/build_rigid_replay.sh',
    'verification/probe/run_rigid_replay.py')]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def hashes():
    return {str(path.relative_to(root)): sha(path) for path in paths}

report = {'passed': False, 'sources_before_build': hashes(), 'game_launched': False, 'bottle': bottle.describe(),
          'cpu_baseline': 'SSE2; stack realignment; four-byte incoming Win32 stack',
          'scope': 'Original fixed SM3 replay plus independent assembly and native GPU conversion/raster proof; finite payload gate retained'}
try:
    subprocess.run(['sh', str(root / 'verification/probe/build_rigid_replay.sh')], check=True, cwd=root)
    subprocess.run(['clang++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                    str(root / 'verification/probe/rigid_replay_profiles.cpp'),
                    str(root / 'src/renderer/rigid_replay_program.cpp'),
                    str(root / 'src/renderer/rigid_position.cpp'), '-o', str(host)], check=True)
    assert hashes() == report['sources_before_build'], 'Source changed during build'
    report['executable_sha256'] = sha(exe)
    report['host_executable_sha256'] = sha(host)
    proof = root / 'verification/results/archive-position-paths.json'
    report['archive_proof_sha256'] = sha(proof)
    programs = [p for p in json.loads(proof.read_text())['programs']
                if p['category'] == 'homogeneous_row_dots']
    raw = {Path('/tmp/x3-shader-sweep/programs') / (p['id'] + '.bin'):
           p['proof']['sha256'] for p in programs}
    assert all(sha(p) == expected for p, expected in raw.items())
    with tempfile.NamedTemporaryFile(mode='w') as manifest:
        for p in programs:
            manifest.write(f"{int(p['model'] == '3_0')} /tmp/x3-shader-sweep/programs/{p['id']}.bin\n")
        manifest.flush()
        profile = subprocess.run([str(host), manifest.name], check=True, capture_output=True, text=True)
    report['profile_result'] = profile.stdout.strip()
    report['local_archive_inputs'] = {p.name: expected for p, expected in raw.items()}
    # Respect a concurrently started user game; never share synthetic GPU work.
    assert not game_running(), 'X3AP running; postpone synthetic GPU verification'
    d3dx = bottle.game_dir() / 'd3dx9_37.dll'
    report['d3dx9_37_sha256'] = sha(d3dx)
    command = ['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine',
               '--bottle', bottle.BOTTLE, '--no-update', '--dll', 'd3d9=b', '--workdir', str(exe.parent),
               str(exe), r'C:\X3\d3dx9_37.dll']
    report['command'] = command
    with (results / 'rigid-replay.txt').open('w') as out, (results / 'rigid-replay-wine.log').open('w') as err:
        run = subprocess.run(command, stdout=out, stderr=err,
                             env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'), timeout=120)
    report['exit_code'] = run.returncode
    text = (results / 'rigid-replay.txt').read_text()
    match = re.search(r'RESULT PASS checks=(\d+) arithmetic_components=(\d+) nan_pairs=(\d+) raster_cases=(\d+) covered=(\d+) empty=(\d+) mismatches=(\d+) finite_gate_retained=1', text)
    report['source_unchanged'] = hashes() == report['sources_before_build']
    report['binary_unchanged'] = sha(exe) == report['executable_sha256'] and sha(host) == report['host_executable_sha256']
    report['compiler_unchanged'] = sha(d3dx) == report['d3dx9_37_sha256']
    report['archive_unchanged'] = sha(proof) == report['archive_proof_sha256'] and all(sha(p) == expected for p, expected in raw.items())
    report['report_sha256'] = sha(results / 'rigid-replay.txt')
    assert run.returncode == 0 and match and 'FAIL' not in text, text[-1800:]
    report['measurements'] = dict(zip(('checks', 'arithmetic_components', 'nan_pairs', 'raster_cases', 'covered', 'empty', 'mismatches'), map(int, match.groups())))
    assert report['measurements']['arithmetic_components'] == (65536 * 3 + 22 * 3) * 8
    assert report['measurements']['raster_cases'] == 134 and report['measurements']['mismatches'] == 0
    assert all(report[key] for key in ('source_unchanged', 'binary_unchanged', 'compiler_unchanged', 'archive_unchanged')), 'Provenance changed'
    report['passed'] = True
finally:
    (results / 'rigid-replay-summary.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: value for key, value in report.items() if key != 'local_archive_inputs'}, indent=2))
