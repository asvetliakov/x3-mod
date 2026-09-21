#!/usr/bin/env python3
"""Freeze and cross-build the standalone real-device observer; never execute it."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-msse2',
         '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2',
         '-ffp-contract=off', '-DX3M_LATTICE_STATE_ONLY_FIXTURE', '-ffunction-sections', '-fdata-sections']
SOURCES = ['verification/probe/lattice_observer_release_fixture.cpp',
           'verification/probe/lattice_state_only_ownership_stubs.cpp',
           'src/proxy/lattice_state_capture.cpp', 'src/proxy/capture_state.cpp']


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(output, inputs):
    output = Path(output).resolve()
    inputs = Path(inputs).resolve()
    # Refuse overwrites, including a prior failed build, so owner-run bytes freeze.
    output.mkdir(parents=True, exist_ok=False)
    retained = output / 'inputs'
    retained.mkdir()
    for name in ('vertex.bin', 'candidate.bin'):
        source = inputs / name
        size = source.stat().st_size
        if size == 0 or size > 65536 or size % 4:
            raise ValueError(f'invalid shader size: {name}')
        shutil.copyfile(source, retained / name)
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler:
        raise RuntimeError('i686-w64-mingw32-g++ unavailable')
    exe = output / 'lattice_observer_release_fixture.exe'
    command = [compiler, *FLAGS, '-I', str(ROOT / 'src/proxy'),
               *[str(ROOT / p) for p in SOURCES], '-Wl,--gc-sections', '-static',
               '-static-libgcc', '-static-libstdc++', '-ld3d9', '-ldxguid', '-ladvapi32', '-o', str(exe)]
    begin = time.monotonic()
    result = subprocess.run(command, capture_output=True, text=True)
    elapsed = time.monotonic() - begin
    (output / 'build.stdout').write_text(result.stdout)
    (output / 'build.stderr').write_text(result.stderr)
    dependencies = SOURCES + ['src/proxy/lattice_state_capture.h', 'src/proxy/lattice_state_policy.h',
                              'src/proxy/lattice_geometry_packet.h', 'src/proxy/lattice_geometry_windows.h',
                              'src/ownership/clone_upload_observer.h', 'src/ownership/clone_upload_core.h',
                              'src/ownership/d3d9_ownership.h', 'src/ownership/buffer_lock_observation.h',
                              'src/proxy/cpu_state.h', 'src/proxy/motion_output.h',
                              'src/proxy/capture_state.h']
    record = dict(command=command, seconds=elapsed, exit_code=result.returncode,
                  toolchain=subprocess.check_output([compiler, '--version'], text=True).splitlines()[0],
                  source_sha256={p: digest(ROOT / p) for p in dependencies},
                  inputs={name: digest(retained / name) for name in ('vertex.bin', 'candidate.bin')},
                  source_inputs=str(inputs), executed=False, selector_bypassed=True,
                  upload_requested=False, ownership_upload_stubbed=True, geometry_payload_tested=False)
    if result.returncode == 0:
        record['exe_sha256'] = digest(exe)
    (output / 'build.json').write_text(json.dumps(record, indent=2) + '\n')
    if result.returncode:
        raise RuntimeError(f'build failed: {output / "build.stderr"}')
    print(json.dumps(dict(exe=str(exe), inputs=str(retained), sha256=record['exe_sha256'])))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    parser.add_argument('--inputs', required=True)
    args = parser.parse_args()
    build(args.output, args.inputs)
