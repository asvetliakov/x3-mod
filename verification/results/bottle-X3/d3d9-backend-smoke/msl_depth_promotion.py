#!/usr/bin/env python3
"""Why the metalsharp DXVK 3.1 fork reads back R,R,R on MoltenVK 1.4.2 (2026-10-08).

Reads a MoltenVK shader dump directory (run the smoke fixture with
--env MVK_CONFIG_SHADER_DUMP_DIR=<dir>; MoltenVK writes shader-{vs,fs,cs}-<hash>.{spv,metal} there)
and prints, per fragment shader: the OpTypeImage declarations of the SPIR-V DXVK handed to MoltenVK
(dimension, Depth flag, Sampled) and the Metal texture types SPIRV-Cross emitted for them, plus whether
the MSL contains sample_compare and a scalar-splat `float4(x.sample(` pattern. A SPIR-V image with
Depth=0 that becomes `depth2d<float>` in MSL is the finding: SPIRV-Cross promotes any image used by a
dref op (DXVK's runtime `if (isDepth) sampleDref else sample` branch, dxbc-spirv
sm3_resources.cpp emitSampleColorOrDref) to a Metal depth texture, whose plain sample() returns one
scalar. Usage: python3 -I msl_depth_promotion.py <dump dir>"""
import re
import struct
import sys
from pathlib import Path

DIMS = {0: '1D', 1: '2D', 2: '3D', 3: 'Cube', 4: 'Rect', 5: 'Buffer', 6: 'SubpassData'}


def spirv_images(path):
    data = path.read_bytes()
    words = struct.unpack('<%dI' % (len(data) // 4), data)
    i, out = 5, []
    while i < len(words):
        op, n = words[i] & 0xffff, words[i] >> 16
        if op == 25:  # OpTypeImage: result, sampled type, Dim, Depth, Arrayed, MS, Sampled, Format
            out.append(f'{DIMS.get(words[i + 3], words[i + 3])} depth={words[i + 4]} sampled={words[i + 7]}')
        i += n
    return out


def main():
    dump = Path(sys.argv[1])
    for spv in sorted(dump.glob('shader-fs-*.spv')):
        msl = spv.with_suffix('.metal').read_text(errors='replace')
        decls = sorted(set(re.findall(r'\b((?:texture|depth)(?:2d|cube|3d)<float>(?:, \d+>)?)\s+(\w+)', msl)))
        print(spv.name, 'spirv:', spirv_images(spv))
        print('   msl:', [f'{t} {n}' for t, n in decls], 'sample_compare=%d' % msl.count('sample_compare('),
              'float4(x.sample)=%d' % len(re.findall(r'float4\(\w+(?:\[\w+\])?\.sample\(', msl)))


if __name__ == '__main__':
    main()
