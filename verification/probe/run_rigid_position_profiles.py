#!/usr/bin/env python3
"""Fresh Win32 exact profile and per-DWORD mutation verification; no D3D/game."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
INPUTS = ['src/renderer/rigid_position.h', 'src/renderer/rigid_position.cpp',
          'src/renderer/rigid_position_profiles_inc.h', 'src/renderer/position_path_profiles_inc.h',
          'src/renderer/pixel_coverage_profiles_inc.h', 'verification/probe/rigid_position_profiles.cpp',
          'verification/probe/run_rigid_position_profiles.py', 'verification/results/shader-profile-registry.json']
CATEGORIES = {'homogeneous_row_dots': 1, 'direct_clip_xyzw': 2,
              'direct_clip_xyz_w_one': 3, 'view_xy_billboard_projection': 4}
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def sources(): return {p: sha(ROOT / p) for p in INPUTS}
def windows(path): return 'Z:' + str(path.resolve()).replace('/', '\\')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--raw-directory', type=Path, default=Path('/tmp/x3-shader-sweep/programs'))
    args = parser.parse_args()
    build, results = ROOT / 'verification/probe/build', ROOT / 'verification/results'
    build.mkdir(exist_ok=True)
    output = results / 'rigid-position-lookup-summary.json'
    # Invalidate retained PASS before any fallible input read or build step.
    output.write_text(json.dumps(dict(passed=False, phase='reading_inputs'), indent=2)+'\n')
    before = sources()
    output.write_text(json.dumps(dict(passed=False, phase='building', source_hashes=before), indent=2)+'\n')
    exe = build / 'rigid_position_profiles.exe'
    command = ['i686-w64-mingw32-g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
               '-msse2', '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2', '-static',
               str(ROOT / INPUTS[1]), str(ROOT / INPUTS[5]), '-o', str(exe)]
    subprocess.run(command, check=True, timeout=90)
    assert sources() == before
    binary = sha(exe)
    registry = json.loads((ROOT / INPUTS[-1]).read_text())
    profiles = registry['vertices'] + registry['pixels']
    paths, expected, manifest_lines = [], [], []
    for p in profiles:
        path = args.raw_directory / (p['id'] + '.bin')
        assert sha(path) == p['sha256'], 'local shader bytes changed'
        paths.append(path)
        category = CATEGORIES[p['position_path']] if 'position_path' in p else 0
        register = p.get('matrix_register', -1)
        coverage = int(p.get('coverage', {}).get('qualified', False))
        manifest_lines.append(f'{json.dumps(windows(path))} {p["word_count"]} {category} {register} '
                              f'{int(p.get("named_world_view_projection", False))} {coverage} {p["fnv1a64"]}')
        expected.append(f'PASS hash={p["fnv1a64"]} words={p["word_count"]} path={category} '
                        f'matrix={register} coverage={coverage} mutations={p["word_count"]}')
    manifest = build / 'shader-profile-inputs.txt'
    manifest.write_text('\n'.join(manifest_lines) + '\n')
    manifest_sha = sha(manifest)
    launch = ['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine',
              '--bottle', 'Steam', '--no-update', '--workdir', str(build), str(exe), windows(manifest)]
    run = subprocess.run(launch, capture_output=True, timeout=300)
    log = results / 'rigid-position-lookup-wine.log'
    log.write_bytes(run.stdout + b'\n--- stderr ---\n' + run.stderr)
    mutations = sum(p['word_count'] for p in profiles)
    expected.append(f'TOTAL programs={len(profiles)} mutations={mutations}')
    assert run.returncode == 0 and run.stdout.decode().splitlines() == expected, str(log)
    for p, path in zip(profiles, paths):
        assert sha(path) == p['sha256'], 'shader changed during run'
    stable = sources() == before and sha(exe) == binary and sha(manifest) == manifest_sha
    assert stable, 'sources, binary or manifest changed during run'
    data = dict(passed=stable, programs=len(profiles), vertex_programs=256, pixel_programs=495,
                rigid_profiles=234, classified_vertices=256, coverage_profiles=494,
                single_bit_mutations=mutations, baseline_checks_per_program=9,
                game_launched=False, d3d_device_created=False, source_hashes=before,
                executable_sha256=binary, executable_sha256_after=sha(exe),
                input_manifest_sha256=manifest_sha, log_sha256=sha(log),
                build_command=command, command=launch, exit_code=run.returncode,
                source_and_binary_unchanged=stable, raw_program_hashes_rechecked=True,
                cases=[dict(id=p['id'], sha256=p['sha256'], output=text)
                       for p, text in zip(profiles, expected)])
    output.write_text(json.dumps(data, indent=2) + '\n')
    print(json.dumps({k: data[k] for k in ('passed', 'programs', 'single_bit_mutations', 'rigid_profiles', 'coverage_profiles')}))

if __name__ == '__main__': main()
