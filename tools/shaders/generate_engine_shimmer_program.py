#!/usr/bin/env python3
"""Compile the engine heat shimmer's ps_3_0 program (src/effects/engine_shimmer_ps.hlsl ->
src/renderer/engine_shimmer_pixel_program_inc.h, provenance verification/results/engine-shimmer-pixel-program.json).

The compilation, validation and record are tools/shaders/generate_rigid_motion_pixel.py's (its helpers are imported:
the native d3dx9_37.dll D3DXCompileShader through compile_rigid_motion_pixel.cpp, D3DXSHADER_OPTIMIZATION_LEVEL3,
entry main); a separate entry point keeps that tool's digest, and with it every record it checks, unchanged. The record
lists this script among its tool sources. --check recompiles and compares the checked-in header and record without
changing them. No D3D device is created; X3AP must nevertheless be stopped.
Run through verification/probe/wine_lock.py with X3M_FIXTURE_BOTTLE=X3.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_rigid_motion_pixel as base  # noqa: E402

ROOT = base.ROOT
SOURCE = ROOT / 'src/effects/engine_shimmer_ps.hlsl'
HEADER = ROOT / 'src/renderer/engine_shimmer_pixel_program_inc.h'
PROVENANCE = ROOT / 'verification/results/engine-shimmer-pixel-program.json'
TARGET = 'ps_3_0'


def compile_program(args):
    expanded, included = base.expand_includes(SOURCE)
    compiler_path = args.d3dx.resolve()
    tools = (base.COMPILER_SOURCE, Path(base.__file__).resolve(), Path(__file__).resolve())
    inputs = (SOURCE, *tools, compiler_path) + tuple(included)
    before = {path: base.sha(path.read_bytes()) for path in inputs}
    with tempfile.TemporaryDirectory(prefix='x3-engine-shimmer-') as directory:
        work = Path(directory)
        exe, binary = work / 'compile.exe', work / 'program.bin'
        compiled = SOURCE
        if included:
            compiled = work / SOURCE.name
            compiled.write_text(expanded)
        subprocess.run(['i686-w64-mingw32-g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-msse2', '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2',
                        '-static', str(base.COMPILER_SOURCE), '-o', str(exe)], check=True)
        subprocess.run([base.bottle.WINE, *base.bottle.wine_args(), '--dll', 'd3dx9_37=n',
                        str(exe), 'Z:' + str(compiler_path), 'Z:' + str(compiled), 'Z:' + str(binary), TARGET],
                       check=True, timeout=300, env=dict(os.environ, WINEDLLOVERRIDES='d3dx9_37=n'))
        data = binary.read_bytes()
    if before != {path: base.sha(path.read_bytes()) for path in inputs}:
        raise RuntimeError('Compilation inputs changed')
    words = base.validate(data, TARGET)
    text = '// Generated from our original %s. Do not edit.\n' % SOURCE.relative_to(ROOT)
    text += ('// Reproduce: X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 '
             'tools/shaders/generate_engine_shimmer_program.py --check\n')
    text += ''.join('    ' + ', '.join(f'0x{v:08x}u' for v in words[i:i + 6]) + ',\n' for i in range(0, len(words), 6))
    record = dict(schema=1, source=str(SOURCE.relative_to(ROOT)), source_sha256=before[SOURCE],
                  compiler='native d3dx9_37.dll D3DXCompileShader', compiler_sha256=before[compiler_path],
                  entry='main', target=TARGET, flags=32768, flags_name='D3DXSHADER_OPTIMIZATION_LEVEL3',
                  defines=None, includes={str(path.relative_to(ROOT)): before[path] for path in included} or None,
                  word_count=len(words), bytecode_sha256=base.sha(data), header_sha256=base.sha(text.encode()),
                  tool_sources={str(path.relative_to(ROOT)): before[path] for path in tools},
                  copyright_scope='Original authored project shader; no game shader bytes',
                  creates_d3d_device=False)
    manifest = json.dumps(record, indent=2) + '\n'
    if args.check:
        if not HEADER.exists() or HEADER.read_text() != text or PROVENANCE.read_text() != manifest:
            raise ValueError('engine_shimmer_ps: embedded shader/provenance differs from current native compilation')
    else:
        HEADER.write_text(text)
        PROVENANCE.write_text(manifest)
    return dict(shader='engine_shimmer_ps', word_count=len(words), bytecode_sha256=base.sha(data))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--d3dx', type=Path, default=base.bottle.game_dir() / 'd3dx9_37.dll')
    args = parser.parse_args()
    if base.game_running():
        raise RuntimeError('X3AP running or process inventory failed; postpone compilation')
    print(json.dumps(dict(result='PASS', check=args.check, shaders=[compile_program(args)])))


if __name__ == '__main__':
    main()
