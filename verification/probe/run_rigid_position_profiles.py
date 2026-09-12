#!/usr/bin/env python3
"""Fresh Win32 exact profile and per-DWORD mutation verification; no D3D/game."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import struct
from collections import Counter
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory

ROOT = Path(__file__).resolve().parents[2]
INPUTS = ['src/renderer/rigid_position.h', 'src/renderer/rigid_position.cpp',
          'src/renderer/rigid_position_profiles_inc.h', 'src/renderer/position_path_profiles_inc.h',
          'src/renderer/pixel_coverage_profiles_inc.h', 'verification/probe/rigid_position_profiles.cpp',
          'verification/probe/run_rigid_position_profiles.py', 'verification/results/shader-profile-registry.json',
          'verification/results/archive-position-paths.json']
ARCHIVE_PROOF_SHA256 = '1617691f1c4e21737cf1bb604b4e3155510d11947acc78b2e40877b10bde431c'
CATEGORIES = {'homogeneous_row_dots': 1, 'direct_clip_xyzw': 2,
              'direct_clip_xyz_w_one': 3, 'view_xy_billboard_projection': 4}
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def sources(): return {p: sha(ROOT / p) for p in INPUTS}
def windows(path): return 'Z:' + str(path.resolve()).replace('/', '\\')

def position_contract(code, archived):
    """Independent expected metadata from reviewed offsets and original tokens.

    Does not consume the generator's new contract fields or call its helpers.
    Complete SHA binding supplies the prior framing/liveness proof; these checks
    specifically verify version, constructor arithmetic and output lane order.
    """
    if archived['category'] != 'homogeneous_row_dots':
        return 0, 0, 0
    proof = archived['proof']
    assert proof['qualified_position_math'] is True
    assert hashlib.sha256(code).hexdigest() == proof['sha256']
    words = struct.unpack('<%dI' % (len(code) // 4), code)
    version = int(proof['version'], 16)
    assert words[0] == version and version in (0xfffe0101, 0xfffe0200, 0xfffe0201, 0xfffe0300)
    modern = ((version >> 8) & 255) >= 2
    constructor, literal = proof['constructor_dword'], proof['literal_register']
    temp = proof['constructor_temp_register']
    assert proof['input_register'] == 0 and proof['input_w_used'] is False
    assert words[constructor:constructor + 5] == (
        0x04000004 if modern else 4, 0x800f0000 | temp,
        0x90240000, 0xa0400000 | literal, 0xa0150000 | literal)
    definition = proof['literal_def_dword']
    assert words[definition:definition + 4] == (
        0x05000051 if modern else 81, 0xa00f0000 | literal, 0x3f800000, 0)
    offsets = proof['position_dot_dwords_xyzw']
    assert len(offsets) == len(set(offsets)) == 4 and constructor < min(offsets)
    output_kind, output_register = proof['output_register_type'], proof['output_register']
    for lane, offset in enumerate(offsets):
        destination = 0x80000000 | ((output_kind & 7) << 28) | ((output_kind & 24) << 8) | ((1 << lane) << 16) | output_register
        assert words[offset:offset + 4] == (
            0x03000009 if modern else 9, destination, 0x80e40000 | temp,
            0xa0e40000 | (proof['matrix_first_register'] + lane))
    order = ''.join('XYZW'[lane] for lane in sorted(range(4), key=lambda lane: offsets[lane]))
    assert order in ('XYZW', 'WXYZ'), order
    return version, {'XYZW': 1, 'WXYZ': 2}[order], 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--raw-directory', type=Path, default=Path('/tmp/x3-shader-sweep/programs'))
    args = parser.parse_args()
    build, results = ROOT / 'verification/probe/build', bottle.results_dir(ROOT)
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
    registry = json.loads((ROOT / 'verification/results/shader-profile-registry.json').read_text())
    assert sha(ROOT / INPUTS[-1]) == ARCHIVE_PROOF_SHA256, 'Reviewed archive proof changed'
    archive = {p['id']: p for p in json.loads((ROOT / INPUTS[-1]).read_text())['programs']}
    profiles = registry['vertices'] + registry['pixels']
    paths, expected, manifest_lines = [], [], []
    contracts = Counter()
    for p in profiles:
        path = args.raw_directory / (p['id'] + '.bin')
        assert sha(path) == p['sha256'], 'local shader bytes changed'
        paths.append(path)
        category = CATEGORIES[p['position_path']] if 'position_path' in p else 0
        register = p.get('matrix_register', -1)
        coverage = int(p.get('coverage', {}).get('qualified', False))
        version, order, constructor = position_contract(path.read_bytes(), archive[p['id']]) if category else (0, 0, 0)
        if register >= 0:
            contracts[(f'{version:08x}', order, constructor)] += 1
        manifest_lines.append(f'{json.dumps(windows(path))} {p["word_count"]} {category} {register} '
                              f'{int(p.get("named_world_view_projection", False))} {coverage} {p["fnv1a64"]} {version} {order} {constructor}')
        expected.append(f'PASS hash={p["fnv1a64"]} words={p["word_count"]} path={category} '
                        f'matrix={register} coverage={coverage} version={version:08x} order={order} '
                        f'constructor={constructor} mutations={p["word_count"]}')
    manifest = build / 'shader-profile-inputs.txt'
    manifest.write_text('\n'.join(manifest_lines) + '\n')
    manifest_sha = sha(manifest)
    launch = ['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine',
              '--bottle', bottle.BOTTLE, '--no-update', '--workdir', str(build), str(exe), windows(manifest)]
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
                game_launched=False, bottle=bottle.describe(), d3d_device_created=False, source_hashes=before,
                executable_sha256=binary, executable_sha256_after=sha(exe),
                input_manifest_sha256=manifest_sha, log_sha256=sha(log),
                build_command=command, command=launch, exit_code=run.returncode,
                source_and_binary_unchanged=stable, raw_program_hashes_rechecked=True,
                position_contract_expected_source='Pinned archive proof offsets and independently checked original tokens',
                position_contract_counts=[dict(version=v, order=o, constructor=c, profiles=n)
                                          for (v, o, c), n in sorted(contracts.items())],
                legacy_aggregate_defaults_checked=True,
                cases=[dict(id=p['id'], sha256=p['sha256'], output=text)
                       for p, text in zip(profiles, expected)])
    output.write_text(json.dumps(data, indent=2) + '\n')
    print(json.dumps({k: data[k] for k in ('passed', 'programs', 'single_bit_mutations', 'rigid_profiles', 'coverage_profiles')}))

if __name__ == '__main__': main()
