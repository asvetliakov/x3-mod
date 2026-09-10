#!/usr/bin/env python3
"""Compile our original motion HLSL with the pinned native D3DX compiler.

--check recompiles and compares both checked-in artifacts without changing them.
The compiler DLL is an external local prerequisite, never redistributed. Only
our authored shader's compiled program and deterministic provenance are retained.
No D3D device is created; X3AP must nevertheless be stopped for this tool.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/temporal/rigid_motion_ps.hlsl'
COMPILER_SOURCE = ROOT / 'tools/shaders/compile_rigid_motion_pixel.cpp'
HEADER = ROOT / 'src/renderer/rigid_motion_pixel_program_inc.h'
PROVENANCE = ROOT / 'verification/results/rigid-motion-pixel-program.json'
DLL_SHA = 'c2ccb84c672a9d8966e82a28005a4269886ee304972ac3590c0b8a9c1622a3d8'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def validate(data):
    if len(data) % 4 or not 8 <= len(data) <= 16384:
        raise ValueError('Unexpected compiled program extent')
    words = struct.unpack('<' + 'I' * (len(data) // 4), data)
    if words[0] != 0xffff0300 or words[-1] != 0x0000ffff:
        raise ValueError('Expected complete ps_3_0 program')
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--d3dx', type=Path, default=Path.home() / 'Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/d3dx9_37.dll')
    args = parser.parse_args()
    active = subprocess.run(['pgrep', '-ifl', '[X]3AP[.]exe'], capture_output=True, text=True)
    if active.returncode != 1 or active.stdout.strip():
        raise RuntimeError('X3AP running or process inventory failed; postpone compilation')
    inputs = (SOURCE, COMPILER_SOURCE, Path(__file__).resolve(), args.d3dx.resolve())
    before = {path: sha(path.read_bytes()) for path in inputs}
    if before[inputs[-1]] != DLL_SHA:
        raise ValueError('Unreviewed D3DX compiler hash')
    with tempfile.TemporaryDirectory(prefix='x3-original-motion-') as directory:
        work = Path(directory)
        exe, binary = work / 'compile.exe', work / 'motion.bin'
        subprocess.run(['i686-w64-mingw32-g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-msse2', '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2',
                        '-static', str(COMPILER_SOURCE), '-o', str(exe)], check=True)
        subprocess.run(['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine',
                        '--bottle', 'Steam', '--no-update', '--dll', 'd3dx9_37=n',
                        str(exe), 'Z:' + str(inputs[-1]), 'Z:' + str(SOURCE), 'Z:' + str(binary)],
                       check=True, timeout=60, env=dict(os.environ, WINEDLLOVERRIDES='d3dx9_37=n'))
        data = binary.read_bytes()
    if before != {path: sha(path.read_bytes()) for path in inputs}:
        raise RuntimeError('Compilation inputs changed')
    words = validate(data)
    text = '// Generated from our original src/temporal/rigid_motion_ps.hlsl. Do not edit.\n'
    text += '// Reproduce: python3 tools/shaders/generate_rigid_motion_pixel.py --check\n'
    text += ''.join('    ' + ', '.join(f'0x{v:08x}u' for v in words[i:i+6]) + ',\n'
                    for i in range(0, len(words), 6))
    record = dict(schema=1, source=str(SOURCE.relative_to(ROOT)), source_sha256=before[SOURCE],
                  compiler='native d3dx9_37.dll D3DXCompileShader', compiler_sha256=DLL_SHA,
                  entry='main', target='ps_3_0', flags=32768, flags_name='D3DXSHADER_OPTIMIZATION_LEVEL3',
                  defines=None, includes=None, word_count=len(words), bytecode_sha256=sha(data),
                  header_sha256=sha(text.encode()),
                  tool_sources={str(path.relative_to(ROOT)): before[path] for path in inputs[1:3]},
                  copyright_scope='Original authored project shader; no game shader bytes',
                  creates_d3d_device=False)
    manifest = json.dumps(record, indent=2) + '\n'
    if args.check:
        if HEADER.read_text() != text or PROVENANCE.read_text() != manifest:
            raise ValueError('Embedded shader/provenance differs from current native compilation')
    else:
        HEADER.write_text(text)
        PROVENANCE.write_text(manifest)
    print(json.dumps(dict(result='PASS', check=args.check, word_count=len(words), bytecode_sha256=sha(data))))


if __name__ == '__main__':
    main()
