#!/usr/bin/env python3
"""Freeze only the standalone connected fixture; root owns invocation and Wine."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from prepare_lav_fixture import verify_record
ROOT = Path(__file__).resolve().parents[2]
SOURCES = (
    'src/media/lav_worker.cpp', 'src/media/package_config.cpp', 'src/media/playback_runtime.cpp',
    'src/proxy/media_playback.cpp', 'src/proxy/media_engine_adapter.cpp', 'src/proxy/media_services.cpp',
    'src/proxy/media_startup.cpp', 'src/proxy/media_destination.cpp', 'src/proxy/media_root.cpp',
    'src/proxy/media_presentation_gate.cpp', 'src/proxy/media_presentation_gate_win32.cpp',
    'src/ownership/application_admission.cpp', 'src/ownership/application_admission_abi.cpp',
    'src/ownership/d3d9_ownership.cpp', 'src/ownership/execution_state.cpp',
    'src/ownership/finite_buffer_evidence.cpp', 'src/ownership/portable_managed_upload.cpp',
    'verification/probe/media_connected_runtime.cpp',
)
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread', '-msse2',
         '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2', '-municode', '-ffunction-sections', '-fdata-sections']
LIBS = ['-ldxguid', '-luser32', '-ladvapi32', '-lamstrmid', '-lstrmiids', '-luuid',
        '-ld3d9', '-lole32', '-loleaut32', '-lversion', '-lddraw']
def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--lav-provider-record', type=Path, required=True)
    parser.add_argument('--compiler', default='i686-w64-mingw32-g++')
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() or output.with_suffix('.build.json').exists():
        parser.error('refuse to overwrite retained fixture/build record')
    provider = args.lav_provider_record.resolve()
    origin = verify_record(provider)
    for source in SOURCES:
        if not (ROOT/source).is_file():
            parser.error('canonical integrated dependency missing: '+source)
    output.parent.mkdir(parents=True, exist_ok=True)
    # Compile with dependency files to bind exactly the source/header inputs used,
    # including the canonical clock/lease/header-only code (no copied overlay).
    objects, commands, inputs = [], [], set()
    objdir = output.parent/'connected-objects'
    objdir.mkdir(exist_ok=False)
    for index, source in enumerate(SOURCES):
        obj = objdir/f'{index}.o'
        dep = objdir/f'{index}.d'
        extra = ['-fno-exceptions'] if source.endswith('application_admission_abi.cpp') else []
        command = [args.compiler, *FLAGS, *extra, '-I'+str(ROOT/'src/media'), '-isystem',
                   str(provider.parent/'include'), '-MMD', '-MF', str(dep), '-c', str(ROOT/source), '-o', str(obj)]
        subprocess.run(command, check=True)
        commands.append(command); objects.append(str(obj))
        # Repository paths contain no whitespace; toolchain/system headers are
        # deliberately omitted by -MMD and provider provenance is bound separately.
        for raw in dep.read_text().replace('\\\n', ' ').split(':', 1)[1].split():
            path = Path(raw).resolve()
            if path.is_relative_to(ROOT): inputs.add(path)
    command = [args.compiler, *FLAGS, '-static', '-static-libgcc', '-static-libstdc++',
               # GCC used emits the map but does not make it a linker GC root.
               # Keep its verified i686 C symbol while unused Root code is pruned.
               '-Wl,--gc-sections', '-Wl,--undefined,_connected_map', '-Wl,--image-base,0x400000', '-Wl,--disable-dynamicbase',
               '-Wl,--section-start,.x3map=0x401000', '-Wl,--section-start,.text=0x630000',
               *objects, '-o', str(output), *LIBS]
    subprocess.run(command, check=True); commands.append(command)
    # Reuse PE layout audit, without importing its build-on-import module.
    from media_connected_evidence import audit_image, audit_counter_contract
    image = audit_image(output)
    counter_contract = audit_counter_contract(ROOT)
    record = dict(schema=1, kind='media_connected_build_v1', commands=commands,
                  toolchain=subprocess.check_output([args.compiler, '--version'], text=True).splitlines()[0],
                  exe=str(output), exe_sha256=digest(output), provider=origin,
                  counter_contract=counter_contract,provider_record=dict(path=str(provider), sha256=digest(provider)), image=image,
                  production_inputs={str(path.relative_to(ROOT)):digest(path) for path in sorted(inputs)},
                  scope='actual runtime/native copy; authored engine handler frames and continuations')
    output.with_suffix('.build.json').write_text(json.dumps(record, indent=2)+'\n')
    print(json.dumps(dict(exe=str(output), sha256=record['exe_sha256'], inputs=len(inputs))))
if __name__ == '__main__':
    main()
