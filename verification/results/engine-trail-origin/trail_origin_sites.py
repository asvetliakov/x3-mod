#!/usr/bin/env python3
"""Byte checks for the Particles3 engine-trail origin study (read-only; prints PASS/FAIL per site and E8 callers).

Owning note: docs/reverse-engineering/engine-trail-origin.md.

  python3 trail_origin_sites.py [X3AP.exe]

The EXE defaults to the native install /mnt/c/X3/X3AP.exe (only read). Expected bytes were taken from a local
`i686-w64-mingw32-objdump -d -M intel` listing of SHA-256 fdbf3418...f8ab; no game bytes are stored beyond these
short instruction patterns.
"""
import hashlib
import re
import struct
import sys
from pathlib import Path

EXE = Path('/mnt/c/X3/X3AP.exe')
CHECKS = [  # (va, expected hex, meaning)
    (0x0041481d, '8b4d08 8d542420 52 8b542414 51 50 52 e83fe5ffff',
     'site B: push &pos ([esp+0x20] local), obj, trail, k; call 0x00412d70'),
    (0x00412e3c, '8b4808 8b5500 83c110 8911 8b5504 895104 8b5508 895108 8b550c 89510c',
     '0x00412d70 refresh: gen = link+8; gen+0x10..+0x1c = *pos (4 dwords)'),
    (0x00412e5e, 'c21000', '0x00412d70 ret 0x10 (stdcall, 4 args)'),
    (0x00416d20, '8b4270 8d4c2430 51 83c040 e840b70d00',
     '0x00416750: EAX = root+0x40 (rotation rows) -> 0x004f2470 -> quaternion [esp+0x30]'),
    (0x00416da1, '52 50 8b4610 50 8d9100010000 52 8d442440 50 83c130 51 57 e8b3530500',
     '0x00416750: call 0x0046c170(gen, root+0x30, &quat, root+0x100, obj+0x10, rot term, now)'),
    (0x0046c1d4, '0391a4000000 2b8180000000 89542424 8974242c 89442428 7807 3de8030000 7e08',
     'emit: state = gen+0xa4 + k*0x20; dt = now - gen+0x80, clamped to 0..1000'),
    (0x0046c374, '8b4518 e8f4d7ffff 03c3 03451c',
     'emit: denominator = |obj speed| + initial speed + rotation term'),
    (0x0046c5c2, '8d442414 8bcf 83c170 50 51 8d4c2430 8dbc24b8000000 c744241ce8030000 e8ebd4ffff',
     'emit: previous velocity gen+0x70 x dt / 1000 (tangent 0)'),
    (0x0046c5e5, '8b4514 8d542414 52 50 8d4c2430 8d7c2478 c744241ce8030000 e8cdd4ffff',
     'emit: current velocity root+0x100 x dt / 1000 (tangent 1)'),
    (0x0046c603, '8d8c24c0000000 51 8d542454 52 8b550c 56 8bc7 8b7d08 50 8d8c24c0000000 51 52 8d4760 50 e854660800',
     'emit: 0x004f2c80 cubic Hermite(gen+0x60, root pos, tangents, u) -> [esp+0x50]'),
    (0x0046c62c, '8b4d10 83c41c 56 81c790000000 8d742464 e8de620800',
     'emit: 0x004f2920 slerp(gen+0x90 previous quat, current quat, u)'),
    (0x0046c645, '8d8c2480000000 8bc6 e8ad5c0800', 'emit: 0x004f2300 quaternion -> rotation rows [esp+0x80]'),
    (0x0046c7f2, '8b7508 83c610 8d7c2440 8d8c2480000000 e898450800',
     'emit: 0x004f0da0 rotates gen+0x10 (emitter offset) -> [esp+0x40]'),
    (0x0046c80f, '8b442450 8b4c2440 8d1401 8b442454 8b4c2444 8d3401 8b442458 8b4c2448 895310',
     'emit: particle+0x10 = Hermite position + rotated offset (x)'),
    (0x0046c8cd, '895110 8b54244c 897114 897918 89511c', 'emit: PEDF_TRAIL state+0x10..+0x1c = new position'),
    (0x0046c97d, '8b4508 8b4d0c 8b5520 899080000000 8b11 895060', 'emit epilogue: gen+0x80 = now, gen+0x60 = root pos'),
    (0x0046c9ed, 'c21c00', '0x0046c170 ret 0x1c (stdcall, 7 args)'),
    (0x00469b70, '85c07d02f7d8c3', '0x00469b70 = abs(EAX)'),
]
CALLEES = {0x00412d70: 'trail generator find-or-create', 0x0046c170: 'emit',
           0x004be7d0: 'particle vertex fill', 0x0046bec0: 'particle move/age'}


def sections(data):
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    opt = struct.unpack_from('<H', data, pe + 20)[0]
    base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
    out = []
    for i in range(count):
        o = pe + 24 + opt + 40 * i
        name = data[o:o + 8].rstrip(b'\0').decode()
        vsize, va, rsize, raw = struct.unpack_from('<IIII', data, o + 8)
        out.append((name, base + va, min(vsize, rsize), raw))
    return out


def main():
    exe = Path(sys.argv[1]) if len(sys.argv) > 1 else EXE
    data = exe.read_bytes()
    secs = sections(data)
    print(f'{exe.name} sha256 {hashlib.sha256(data).hexdigest()[:16]}')

    def rd(va, n):
        for _, start, size, raw in secs:
            if start <= va < start + size:
                return data[raw + va - start: raw + va - start + n]
        raise ValueError(hex(va))

    ok = 0
    for va, hexs, what in CHECKS:
        want = bytes.fromhex(hexs.replace(' ', ''))
        got = rd(va, len(want))
        good = got == want
        ok += good
        print(f'{"PASS" if good else "FAIL"} {va:#010x} {len(want):3d} B  {what}')
    text = next(s for s in secs if s[0] == '.text')
    tb = data[text[3]:text[3] + text[2]]
    for target, what in CALLEES.items():
        callers = []
        for m in re.finditer(rb'\xe8', tb):
            off = m.start()
            if off + 5 <= len(tb) and text[1] + off + 5 + struct.unpack_from('<i', tb, off + 1)[0] == target:
                callers.append(text[1] + off)
        print(f'E8 callers of {target:#010x} ({what}): ' + ' '.join(f'{c:#010x}' for c in callers))
    print(f'{ok}/{len(CHECKS)} PASS')
    return 0 if ok == len(CHECKS) else 1


if __name__ == '__main__':
    sys.exit(main())
