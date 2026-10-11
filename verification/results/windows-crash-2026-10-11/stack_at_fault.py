#!/usr/bin/env python3
"""Print dwords at ESP of the faulting thread and node fields at EDI from X3Crash.dmp.
Usage: stack_at_fault.py <dmp> <esp_hex> <edi_hex>"""
import struct, sys
d = open(sys.argv[1], 'rb').read(); esp = int(sys.argv[2], 16); edi = int(sys.argv[3], 16)
u32 = lambda o: struct.unpack_from('<I', d, o)[0]
nst, rva = u32(8), u32(12); mem = []
for i in range(nst):
    t, sz, srva = struct.unpack_from('<III', d, rva + 12 * i)
    if t == 5:
        for k in range(u32(srva)):
            o = srva + 4 + 16 * k
            mem.append((struct.unpack_from('<Q', d, o)[0], u32(o + 8), u32(o + 12)))
def rd(a):
    for s, n, r in mem:
        if s <= a and a + 4 <= s + n: return u32(r + a - s)
    return None
for k in range(0, 0x80, 4):
    v = rd(esp + k); print(f'[esp+{k:#04x}] = {v:08x}' if v is not None else f'[esp+{k:#04x}] not captured')
for off in (0x0, 0x4, 0x1c, 0x140):
    v = rd(edi + off); print(f'node[{off:#x}] = ' + (f'{v:08x}' if v is not None else 'not captured'))
