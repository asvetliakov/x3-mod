#!/usr/bin/env python3
"""Compile the muzzle flash re-shade's ps_3_0 program (docs/architecture/muzzle-flash-look.md):
src/effects/weapon_muzzle_reshade_ps.hlsl -> src/renderer/weapon_muzzle_reshade_pixel_program_inc.h (provenance
verification/results/weapon-muzzle-reshade-pixel-program.json).

ps_3_0 because the program replaces the game's standard_lighting(2s) DEFAULT pixel shader beside the game's vs_3_0
vertex shader 494fe349b8bc12ec. The helpers are tools/shaders/generate_rigid_motion_pixel.py's and
generate_weapon_effects_programs.py's (imported, so their digests and every record they check stay unchanged); the
compiler is tools/shaders/compile_rigid_motion_pixel.cpp (ps_3_0), the native d3dx9_37.dll D3DXCompileShader with
D3DXSHADER_OPTIMIZATION_LEVEL3, entry main. --check recompiles and compares the checked-in header and record without
changing them. No D3D device is created; X3AP / X3FL must be stopped.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_rigid_motion_pixel as base  # noqa: E402
import generate_weapon_effects_programs as weapons  # noqa: E402

ROOT = base.ROOT
PROGRAMS = {
    'weapon_muzzle_reshade_ps': (ROOT / 'src/effects/weapon_muzzle_reshade_ps.hlsl',
                                 ROOT / 'src/renderer/weapon_muzzle_reshade_pixel_program_inc.h',
                                 ROOT / 'verification/results/weapon-muzzle-reshade-pixel-program.json'),
}
COMPILER_SOURCE = ROOT / 'tools/shaders/compile_rigid_motion_pixel.cpp'
TARGET = 'ps_3_0'
VERSION = 0xffff0300


def validate(data):
    if len(data) % 4 or not 8 <= len(data) <= 4096:
        raise ValueError('Unexpected compiled program extent')
    words = struct.unpack('<' + 'I' * (len(data) // 4), data)
    if words[0] != VERSION or words[-1] != 0x0000ffff:
        raise ValueError('Expected complete %s program' % TARGET)
    offset = 1
    while offset < len(words) - 1:
        token = words[offset]
        if token == 0x0000ffff:
            raise ValueError('Early END')
        count = (token >> 16) & 0x7fff if token & 0xffff == 0xfffe else (token >> 24) & 15
        offset += 1 + count
    if offset != len(words) - 1:
        raise ValueError('Invalid instruction/comment framing')
    return words


def compile_program(name, args):
    SOURCE, HEADER, PROVENANCE = PROGRAMS[name]
    expanded, included = base.expand_includes(SOURCE)
    compiler_path = args.d3dx.resolve()
    tools = (COMPILER_SOURCE, Path(base.__file__).resolve(), Path(weapons.__file__).resolve(), Path(__file__).resolve())
    inputs = (SOURCE, *tools, compiler_path) + tuple(included)
    before = {path: base.sha(path.read_bytes()) for path in inputs}
    with tempfile.TemporaryDirectory(prefix='x3-muzzle-reshade-') as directory:
        work = Path(directory)
        exe, binary = work / 'compile.exe', work / 'program.bin'
        compiled = work / SOURCE.name
        compiled.write_text(expanded)
        subprocess.run(['i686-w64-mingw32-g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-msse2', '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2',
                        '-static', str(COMPILER_SOURCE), '-o', str(exe)], check=True)
        weapons.run_compiler(exe, compiler_path, compiled, binary, TARGET)
        data = binary.read_bytes()
    if before != {path: base.sha(path.read_bytes()) for path in inputs}:
        raise RuntimeError('Compilation inputs changed')
    words = validate(data)
    text = '// Generated from our original %s. Do not edit.\n' % SOURCE.relative_to(ROOT)
    text += '// Reproduce: python3 tools/shaders/generate_muzzle_reshade_program.py --check (native host)\n'
    text += ''.join('    ' + ', '.join(f'0x{v:08x}u' for v in words[i:i + 6]) + ',\n' for i in range(0, len(words), 6))
    record = dict(schema=1, source=str(SOURCE.relative_to(ROOT)), source_sha256=before[SOURCE],
                  compiler='native d3dx9_37.dll D3DXCompileShader', compiler_sha256=before[compiler_path],
                  entry='main', target=TARGET, flags=32768, flags_name='D3DXSHADER_OPTIMIZATION_LEVEL3',
                  defines=None, includes={str(path.relative_to(ROOT)): before[path] for path in included} or None,
                  word_count=len(words), bytecode_sha256=base.sha(data),
                  header_sha256=base.sha(text.encode()),
                  tool_sources={str(path.relative_to(ROOT)): before[path] for path in tools},
                  copyright_scope='Original authored project shader; no game shader bytes',
                  creates_d3d_device=False)
    manifest = json.dumps(record, indent=2) + '\n'
    if args.check:
        if not HEADER.exists() or HEADER.read_text() != text or PROVENANCE.read_text() != manifest:
            raise ValueError(f'{name}: embedded shader/provenance differs from current native compilation')
    else:
        HEADER.write_text(text)
        PROVENANCE.write_text(manifest)
    return dict(shader=name, word_count=len(words), bytecode_sha256=base.sha(data))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--d3dx', type=Path, default=base.bottle.game_dir() / 'd3dx9_37.dll')
    args = parser.parse_args()
    if weapons.game_running():
        raise RuntimeError('X3AP / X3FL running or process inventory failed; postpone compilation')
    print(json.dumps(dict(result='PASS', check=args.check, shaders=[compile_program(name, args) for name in PROGRAMS])))


if __name__ == '__main__':
    main()
