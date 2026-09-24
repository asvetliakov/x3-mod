#!/usr/bin/env python3
"""Byte checks for the FOV write sites in the installed X3AP.exe (read-only; prints hex of a few
instruction spans and any rel8/rel32 branch in .text that lands inside the 9-byte base-load span)."""
import hashlib, struct
from pathlib import Path
exe = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe'
d = exe.read_bytes()
print('X3AP.exe sha256', hashlib.sha256(d).hexdigest())
pe = struct.unpack_from('<I', d, 0x3c)[0]; n = struct.unpack_from('<H', d, pe + 6)[0]; opt = struct.unpack_from('<H', d, pe + 20)[0]
for i in range(n):
    o = pe + 24 + opt + 40 * i
    if d[o:o + 5] == b'.text':
        va = struct.unpack_from('<I', d, o + 12)[0] + 0x400000; raw = struct.unpack_from('<I', d, o + 20)[0]; size = struct.unpack_from('<I', d, o + 16)[0]
code = d[raw:raw + size]
sites = {
    'registry ctor  MOV [ESI+0x24],0x4000': (0x0041c9d9, 7),
    'cockpit ctor   MOV EDX,[0x00608504]': (0x0041fd58, 6),
    'cockpit ctor   MOV EAX,[EDX+0x24]': (0x0041fd95, 3),
    'cockpit update CMP [EBX+0x10],0': (0x00421144, 4),
    'cockpit update MOV EDX,[0x00608504]; MOV ESI,[EDX+0x24]': (0x00421148, 9),
    'cockpit update MOV EDI,0x4000': (0x00421151, 5),
    'cockpit update MOV EDX,0x10000; JZ 0x00421588': (0x0042115e, 11),
    'INS_SetFocus   MOV [EDX+0x24],ECX': (0x0042dc04, 3),
    'chase_lead_final_fov CMP [EBX+0x230],ESI': (0x004213dd, 6),
}
for k, (a, l) in sites.items():
    print(f'{a:#010x} {code[a - va:a - va + l].hex(" "):32s} {k}')
lo, hi = 0x00421149, 0x00421150
hits = []
for i in range(len(code) - 6):
    b = code[i]
    if b in (0xe8, 0xe9): t, w = va + i + 5 + struct.unpack_from('<i', code, i + 1)[0], 'rel32'
    elif b == 0x0f and 0x80 <= code[i + 1] <= 0x8f: t, w = va + i + 6 + struct.unpack_from('<i', code, i + 2)[0], 'jcc32'
    elif b == 0xeb or 0x70 <= b <= 0x7f: t, w = va + i + 2 + struct.unpack_from('<b', code, i + 1)[0], 'rel8'
    else: continue
    if lo <= t <= hi: hits.append((hex(va + i), hex(t), w))
print('byte-pattern branches into 0x00421149..0x00421150 (instruction starts unverified):', hits)
