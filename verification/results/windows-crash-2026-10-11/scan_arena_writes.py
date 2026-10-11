#!/usr/bin/env python3
"""Scan the captured engine_patch arena in X3Crash.dmp for abs32 operands that
point back into the arena, and classify the opcode bytes in front of each:
write forms (inc/dec/add/sub/mov to [abs32], lock-prefixed too) vs reads/jmp [slot].
Usage: scan_arena_writes.py <dmp> <arena_base_hex> <arena_size_hex>"""
import struct, sys
d = open(sys.argv[1], 'rb').read(); base = int(sys.argv[2], 16); size = int(sys.argv[3], 16)
u32 = lambda o: struct.unpack_from('<I', d, o)[0]
nst, rva = u32(8), u32(12)
blob = None
for i in range(nst):
    t, sz, srva = struct.unpack_from('<III', d, rva + 12 * i)
    if t == 5:
        n = u32(srva)
        for k in range(n):
            o = srva + 4 + 16 * k
            start = struct.unpack_from('<Q', d, o)[0]; msz, mrva = u32(o + 8), u32(o + 12)
            if start == base: blob = d[mrva:mrva + msz]
assert blob is not None, 'arena not captured'
print(f'arena captured bytes={len(blob):#x}')
WRITES = {(0xff, 0x05): 'inc [abs]', (0xff, 0x0d): 'dec [abs]', (0x89, 0x05): 'mov [abs],eax',
          (0x89, 0x0d): 'mov [abs],ecx', (0x89, 0x15): 'mov [abs],edx', (0x89, 0x1d): 'mov [abs],ebx',
          (0x89, 0x35): 'mov [abs],esi', (0x89, 0x3d): 'mov [abs],edi', (0x01, 0x05): 'add [abs],eax',
          (0x83, 0x05): 'add [abs],imm8', (0x83, 0x2d): 'sub [abs],imm8', (0x81, 0x05): 'add [abs],imm32',
          (0xc7, 0x05): 'mov [abs],imm32', (0x0f, 0xc1): 'xadd'}
writes = 0; refs = 0
for p in range(2, len(blob) - 3):
    v = struct.unpack_from('<I', blob, p)[0]
    if not (base <= v < base + size): continue
    refs += 1
    op = (blob[p - 2], blob[p - 1]); kind = WRITES.get(op)
    if op == (0xff, 0x25): kind = 'jmp [slot] (read)'
    if op == (0xa3,) or blob[p - 1] == 0xa3: kind = kind or 'mov [abs],eax (a3)?'
    lock = blob[p - 3] == 0xf0 if p >= 3 else False
    is_write = kind and not kind.startswith('jmp') and not kind.endswith('?')
    writes += bool(is_write)
    print(f'{base + p - 2:08x} op={op[0]:02x} {op[1]:02x} target={v:08x} {"lock " if lock else ""}{kind or "data/other"}')
print(f'refs_into_arena={refs} write_forms={writes}')
