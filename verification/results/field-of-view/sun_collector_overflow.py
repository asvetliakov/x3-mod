#!/usr/bin/env python3
"""Static evidence for field-of-view.md §9: the lens collector's horizontal off-screen test overflows at large F.

Reads the installed X3AP.exe (read-only) and prints the instructions of the collector gate
0x0047e315..0x0047e3fc, the TSuns frustum bypass in 0x004c6aa0, and the fixed-point overflow bound
z_crit = 2^32 / (W * tan(F/2)) of `|x|/2 >= FixMul(W, FixMul(tan, z/2))` for display planes and F values.
FixMul(a,b) = (a*b + 0x8000) >> 16 with the 64-bit product truncated to 32 bits by `shrd eax,edx,16`.
"""
import math, struct
from pathlib import Path
import capstone
d = (Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe').read_bytes()
pe = struct.unpack_from('<I', d, 0x3c)[0]; ns = struct.unpack_from('<H', d, pe + 6)[0]; opt = struct.unpack_from('<H', d, pe + 20)[0]
for i in range(ns):
    o = pe + 24 + opt + 40 * i
    if d[o:o + 5] == b'.text': va = struct.unpack_from('<I', d, o + 12)[0] + 0x400000; raw = struct.unpack_from('<I', d, o + 20)[0]; size = struct.unpack_from('<I', d, o + 16)[0]
text = d[raw:raw + size]
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
def listing(a, b):
    for x in md.disasm(text[a - va:b - va], a): print(f'  {x.address:#010x} {x.bytes.hex(" "):24s} {x.mnemonic} {x.op_str}')
print('collector gate (z > 100, z > 2r, then the x test):'); listing(0x0047e315, 0x0047e39d)
print('y test (H):'); listing(0x0047e3ca, 0x0047e402)
print('plane load (W -> [esp+0x18], H -> [esp+0x14]):'); listing(0x0047e23b, 0x0047e26e)
print('frustum test 0x004c6aa0 bypass for node flags 0x20c80000 (TSuns 0x20000000) -> return 1:'); listing(0x004c6ac2, 0x004c6adb); listing(0x004c6e96, 0x004c6ea2)
print()
def W_of(w, h):
    r = (h << 16) // w
    return (0x10000 if r > 0xC000 else (0xC000 * w) // h) / 65536.0
print('z_crit = 2^32/(W*tan(F/2)); the x test reports "off-screen" for any z >= z_crit (z < 2^31 always)')
for (w, h) in ((1920, 1080), (2560, 1080), (3440, 1440), (5120, 1440)):
    W = W_of(w, h)
    row = []
    for F in (0x3470, 0x4000, 0x3b6f, 0x471c):
        zc = 2 ** 32 / (W * math.tan(F * math.pi / 65536))
        row.append(f'F=0x{F:04x}: {zc:.4g}{"*" if zc < 2 ** 31 else ""}')
    print(f'{w}x{h} W={W:.5f}  ' + '  '.join(row))
print('(* = reachable, below 2^31)')
zc = 2 ** 32 / (W_of(5120, 1440) * math.tan(0x471c * math.pi / 65536))
for th in (27.5, 30.0, 32.5):
    print(f'run309 boundary {th} deg at F=0x471c, 5120x1440 -> sun distance D = z_crit/cos = {zc / math.cos(math.radians(th)):.4g} (inferred)')
