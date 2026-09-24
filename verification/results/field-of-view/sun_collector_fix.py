#!/usr/bin/env python3
"""Design evidence for field-of-view.md §9.1: the lens collector's horizontal off-screen test fix.

Reads the installed X3AP.exe (read-only): prints the site bytes 0x0047e365..0x0047e39d with instruction
boundaries and qword placement, checks that nothing branches into the claimed span, assembles the proposed
stub bytes, and emulates the engine arithmetic (vanilla, saturating stub, one-byte JAE variant) on the
fixture vectors to give the expected branch per case.
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
print('site 0x0047e365..0x0047e39d:')
for x in md.disasm(text[0x0047e365 - va:0x0047e39d - va], 0x0047e365):
    print(f'  {x.address:#010x} {x.bytes.hex(" "):20s} {x.mnemonic} {x.op_str}   qword {x.address & ~7:#010x}')
span = (0x0047e391, 6)
print(f'claim span {span[0]:#010x} len {span[1]}: {text[span[0] - va:span[0] - va + span[1]].hex(" ")};'
      f' first five bytes in one aligned qword: {(span[0] // 8) == ((span[0] + 4) // 8)}')
print(f'JGE opcode byte 0x0047e398 = {text[0x0047e398 - va]:#04x} (0x8d; JAE would be 0x83); rel32 {struct.unpack_from("<i", text, 0x0047e399 - va)[0]:#x} -> 0x0047e5b6')
hits = []
for i in range(len(text) - 6):
    b = text[i]
    if b in (0xe8, 0xe9): t = va + i + 5 + struct.unpack_from('<i', text, i + 1)[0]
    elif b == 0x0f and 0x80 <= text[i + 1] <= 0x8f: t = va + i + 6 + struct.unpack_from('<i', text, i + 2)[0]
    elif b == 0xeb or 0x70 <= b <= 0x7f: t = va + i + 2 + struct.unpack_from('<b', text, i + 1)[0]
    else: continue
    if 0x0047e365 <= t <= 0x0047e39c: hits.append((hex(va + i), hex(t)))
print('byte-pattern branches into 0x0047e365..0x0047e39c:', hits,
      '; absolute dword refs:', sum(d.count(struct.pack('<I', a)) for a in range(0x0047e365, 0x0047e39d)))
print('branch targets of the gate window 0x0047e315..0x0047e402:',
      sorted({x.op_str for x in md.disasm(text[0x0047e315 - va:0x0047e402 - va], 0x0047e315) if x.mnemonic.startswith('j')}))
stub = bytes.fromhex('81 fa 00 80 00 00'   # cmp edx,0x8000      ; (EDX:EAX)>>16 >= 2^31 ?
                     '7c 0a'               # jl  +10             ; no overflow (or negative): unchanged
                     'ba ff 7f 00 00'      # mov edx,0x7fff
                     'b8 ff ff ff ff'      # mov eax,0xffffffff  ; shrd eax,edx,16 -> 0x7fffffff
                     'ff 25')              # jmp [continuation] (+ abs32 slot) -> tail: shrd; cmp; jmp 0x0047e397
print('stub a (saturating, runs before the displaced SHRD/CMP):', stub.hex(' '), '+ <abs32 slot>')
for x in md.disasm(stub + b'\0\0\0\0', 0x1000): print(f'  +{x.address - 0x1000:02x} {x.bytes.hex(" "):20s} {x.mnemonic} {x.op_str}')

def fixmul(a, b):                      # imul; add 0x8000; adc; shrd 16 -> low 32 bits, signed view
    p = (a * b + 0x8000) >> 16
    lo = p & 0xffffffff
    return lo - (1 << 32) if lo & 0x80000000 else lo, p
def gate(W, H, tan, x, y, z, r, mode):
    if not (z > 100) or not (2 * r < z): return 'off(z)'
    ax = abs(int(x / 2)); t1, _ = fixmul(tan, int(z / 2))
    bound, full = fixmul(W, t1)
    if mode == 'sat' and full >= 1 << 31: bound = 0x7fffffff
    off = (ax & 0xffffffff) >= (bound & 0xffffffff) if mode == 'jae' else ax >= bound
    if off: return 'off(x)->0x0047e5b6'
    ay = abs(int(y / 2)); t2, _ = fixmul(tan, int(z / 2)); bh, _ = fixmul(H, t2)
    return 'off(y)->0x0047e5b6' if ay >= bh else 'on->0x0047e402'
def tan16(F): return int(round(math.tan(F * math.pi / 65536) * 65536))
W32 = (0xC000 * 5120) // 1440; W169 = (0xC000 * 1920) // 1080; H = 0xC000
cases = [
    ('A 32:9 F=0x471c centre z=1.5e9 (run309 bug)', W32, 0x471c, 0, 0, 1_500_000_000),
    ('B 32:9 F=0x471c centre z=1.2e9 (below z_crit)', W32, 0x471c, 0, 0, 1_200_000_000),
    ('C 32:9 F=0x4000 centre z=1.7e9 (vanilla bug)', W32, 0x4000, 0, 0, 1_700_000_000),
    ('D 32:9 F=0x471c x=-1.05*z*W*tan (off left) z=5e8', W32, 0x471c, -1.05, 0, 500_000_000),
    ('E 32:9 F=0x471c x=0.99*z*W*tan (inside edge) z=5e8', W32, 0x471c, 0.99, 0, 500_000_000),
    ('E2 32:9 F=0x471c x=2^31-1 (largest int32) z=1.5e9', W32, 0x471c, 2**31 - 1, 0, 1_500_000_000),
    ('F 32:9 F=0x471c y=z (off-screen up) z=1.5e9', W32, 0x471c, 0, 1_500_000_000, 1_500_000_000),
    ('G 16:9 F=0x3470 centre z=2.1e9 (no overflow)', W169, 0x3470, 0, 0, 2_100_000_000),
    ('H 32:9 F=0x3470 centre z=2147483000 (at the bound)', W32, 0x3470, 0, 0, 2_147_483_000),
]
print('\nfixture vectors (W, H=0xc000, tan16=round(tan(F/2)*65536), r=1000):')
print(f'{"case":55s} {"W":>7s} {"tan16":>7s} {"z":>11s} | {"vanilla":20s} {"stub a":20s} {"JAE":20s}')
for name, W, F, x, y, z in cases:
    t = tan16(F)
    if isinstance(x, float): x = int(x * z * (W / 65536) * (t / 65536))
    assert -2**31 <= x < 2**31 and 0 < z < 2**31
    res = [gate(W, H, t, x, y, z, 1000, m) for m in ('vanilla', 'sat', 'jae')]
    print(f'{name:55s} {W:7d} {t:7d} {z:11d} | {res[0]:20s} {res[1]:20s} {res[2]:20s}')
print('\nOverflow implies on-screen: when W*t1 >> 16 >= 2^31 the true bound exceeds every |x|/2 <= 2^30, so saturating to')
print('0x7fffffff (stub a) or skipping the test (b) gives the exact answer for every int32 x.')
print('\nJAE variant limit: bound exact while W*tan(F/2)*z/2 < 2^32, i.e. W*tan(F/2) < 4 for any int32 z;')
for asp, W in (('32:9', W32 / 65536), ('48:9', (0xC000 * 48 // 9) / 65536)):
    print(f'  {asp}: W={W:.4f} -> safe while tan(F/2) < {4 / W:.4f}, F < {math.degrees(2 * math.atan(4 / W)):.2f} deg ({round(2 * math.atan(4 / W) * 65536 / math.pi):#06x})')
