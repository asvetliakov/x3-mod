#!/usr/bin/env python3
"""Host-only build of the effects stage fixtures (effects_stage_fixture.cpp); refuses stale embedded shaders.

Two executables: the GPU fixture (the production EffectsStagePass and TemporalPass) and the keys fixture (the same
source with X3M_EFFECTS_KEYS_FIXTURE, linking the ownership layer for the upload-time texture keys). No Wine.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROBE = ROOT / 'verification/probe'
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-msse2', '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2',
         '-static', '-static-libgcc', '-static-libstdc++', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-DX3M_EFFECTS_STAGE_FIXTURE']
PROGRAMS = ('effects_bolt_vs', 'effects_bolt_ps', 'effects_shell_vs', 'effects_shell_ps', 'effects_decal_vs', 'effects_decal_ps')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def shaders_current():
    """Every embedded effects program must match its provenance record (source and header hashes)."""
    records = {}
    for name in PROGRAMS:
        record = json.loads((ROOT / ('verification/results/%s-program.json' % name.replace('_', '-').replace('-vs', '-vertex').replace('-ps', '-pixel'))).read_text())
        source = ROOT / record['source']
        header = ROOT / ('src/renderer/%s_program_inc.h' % name.replace('_vs', '_vertex').replace('_ps', '_pixel'))
        if record['source_sha256'] != digest(source) or record['header_sha256'] != digest(header):
            raise ValueError('%s: stale embedded shader; run the generator (tools/shaders/generate_rigid_motion_pixel.py --shader %s)' % (name, name))
        for included, sha in (record.get('includes') or {}).items():
            if digest(ROOT / included) != sha:
                raise ValueError('%s: stale include %s' % (name, included))
        records[name] = record
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=PROBE / 'build/effects-stage')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    shaders = shaders_current()
    gpu_sources = [PROBE / 'effects_stage_fixture.cpp', ROOT / 'src/renderer/effects_stage_pass.cpp', ROOT / 'src/renderer/temporal_pass.cpp']
    gpu = out / 'effects_stage_fixture.exe'
    command = ['i686-w64-mingw32-g++', *FLAGS, *map(str, gpu_sources), '-o', str(gpu), '-luser32', '-ldxguid']
    subprocess.run(command, check=True)
    # The keys fixture: the ownership layer and its admission objects (verification/probe/build_admission_dependencies.sh).
    admission = out / 'admission'
    subprocess.run(['sh', str(PROBE / 'build_admission_dependencies.sh'), str(admission)], check=True, cwd=PROBE)
    keys_sources = [*gpu_sources, ROOT / 'src/ownership/d3d9_ownership.cpp', ROOT / 'src/ownership/execution_state.cpp', ROOT / 'src/ownership/finite_buffer_evidence.cpp',
                    ROOT / 'src/ownership/portable_managed_upload.cpp']
    keys = out / 'effects_stage_keys_fixture.exe'
    keys_command = ['i686-w64-mingw32-g++', *FLAGS, '-DX3M_EFFECTS_KEYS_FIXTURE', '-pthread', *map(str, keys_sources), str(admission / 'application_admission.o'),
                    str(admission / 'application_admission_abi.o'), '-o', str(keys), '-luser32', '-ldxguid', '-ladvapi32']
    subprocess.run(keys_command, check=True)
    inputs = [*keys_sources, ROOT / 'src/renderer/effects_stage_pass.h', ROOT / 'src/proxy/effects_stage_core.h', ROOT / 'src/renderer/temporal_pass.h', ROOT / 'src/ownership/d3d9_ownership.h',
              ROOT / 'src/ownership/d3d9_forwarders_inc.h', ROOT / 'src/ownership/d3d9_classes_inc.h', Path(__file__).resolve()]
    for name in PROGRAMS:
        inputs.append(ROOT / ('src/renderer/%s_program_inc.h' % name.replace('_vs', '_vertex').replace('_ps', '_pixel')))
        inputs.append(ROOT / shaders[name]['source'])
    record = dict(executable_sha256=digest(gpu), keys_executable_sha256=digest(keys), commands=[command, keys_command],
                  inputs={str(p.relative_to(ROOT)): digest(p) for p in inputs}, shaders=shaders)
    (out / 'build.json').write_text(json.dumps(record, indent=2) + '\n')
    print(gpu)
    print(keys)


if __name__ == '__main__':
    main()
