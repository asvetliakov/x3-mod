#!/usr/bin/env python3
"""Re-read every byte string quoted in docs/reverse-engineering/ship-movement.md section 13 (order-5 remainder
writes, the slip term, the order-8 launch) from X3AP.exe through the PE section map; print RESULT PASS / FAIL.

  python3 verification/results/ship-movement/order5/check_sites.py [path/to/X3AP.exe]

Checks: the image SHA-256; each quoted instruction byte string at its VA; the command-word jump table entries for
words 5, 8 and 0x21 (0x00453ce0: index bytes 0x00459694, targets 0x0045963c); and that each listed site is an
instruction start in a linear capstone sweep of its containing range (needs python3-capstone).
"""
import hashlib
import struct
import sys

EXE = sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3/X3AP.exe'
SHA256 = 'fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab'
data = open(EXE, 'rb').read()
pe = struct.unpack_from('<I', data, 0x3c)[0]
nsec = struct.unpack_from('<H', data, pe + 6)[0]
opt = struct.unpack_from('<H', data, pe + 20)[0]
BASE = 0x400000
SECTIONS = []
for i in range(nsec):
    s = pe + 24 + opt + 40 * i
    vsize, rva, rsize, raw = struct.unpack_from('<IIII', data, s + 8)
    SECTIONS.append((rva, max(vsize, rsize), raw))


def off(va):
    rva = va - BASE
    for srva, size, raw in SECTIONS:
        if srva <= rva < srva + size:
            return raw + rva - srva
    raise ValueError(hex(va))


def rd(va, n):
    return data[off(va):off(va) + n]


fails = []
SITES = [
    # --- order 5 (0x00453fc2), port record and the port-roll match after the leaf ---
    (0x0045405e, 'e8 0d de ff ff 89 84 24 c0 02 00 00', 'port data = 0x00451e70(...) -> [esp+0x2c0] (0 for gates: port index -1)'),
    (0x00454728, '8b 43 50 05 30 09 00 00 33 f6 53', 'approach branch: f+0x930 cleared, push obj'),
    (0x00454745, 'e8 76 85 ff ff', 'call leaf 0x0044ccc0 (site A)'),
    (0x0045474a, '8b 8c 24 c0 02 00 00 3b ce 0f 84 80 00 00 00', 'no port data -> skip the match'),
    (0x00454760, '66 83 78 48 12 74 72', 'target class 0x12 (gate) -> skip the match'),
    (0x00454767, '8b 43 30 3b c6 7d 02 f7 d8 3d 1e 05 00 00 7d 62', '|yaw| < 0x51e'),
    (0x00454777, '8b 43 34 3b c6 7d 02 f7 d8 3d 1e 05 00 00 7d 52', '|pitch| < 0x51e'),
    (0x00454787, '8b 43 38 3b c6 7d 02 f7 d8 3d 1e 05 00 00 7d 42', '|roll| < 0x51e'),
    (0x004547a4, '81 c1 c0 00 00 00 e8 91 be 09 00', 'Euler angles of the port matrix (port data + 0xc0) via 0x004f0640'),
    (0x004547af, '8b 84 24 54 05 00 00 2b 43 20', 'port gamma - ship gamma (obj+0x20)'),
    (0x004547bc, '3d 00 80 00 00 7e 07 2d 00 00 01 00 eb 0c 3d 00 80 ff ff 7d 05 05 00 00 01 00', 'wrap to +-0x8000'),
    (0x004547d6, '89 43 38', 'obj+0x38 = port roll error'),
    # --- order 5 final zone (no leaf call) ---
    (0x004543d2, '24 01 0f 85 f4 02 00 00', 'port entry flag bit 20 -> distance arrival, no final zone'),
    (0x004543f3, '33 c0 39 84 24 c0 02 00 00 89 43 14 89 43 10 89 43 60 89 43 64 89 43 68 89 43 6c', 'final zone: desired, speed, slip = 0'),
    (0x0045451b, '8b 4b 70 33 c0 89 43 30 89 43 34 89 43 38', 'final zone: remainders = 0'),
    (0x004542b7, 'c7 84 24 d8 02 00 00 1c 07 00 00', 'final-zone turn cap 0x71c binary/s (default)'),
    (0x004542e4, 'c7 84 24 d8 02 00 00 8e 03 00 00', 'final-zone turn cap 0x38e for class codes 0,1,3,4,8,9,0xb-0xf'),
    (0x00454540, 'e8 0b d7 09 00', 'relative rotation ship -> port (0x004f1c50)'),
    (0x0045457c, '3d 34 b3 ff ff 0f 8e c5 00 00 00', 'axis dot with f+0x930 <= -0.3 -> stop turning'),
    (0x00454587, '8b 84 24 d8 02 00 00 8b 94 24 ec 02 00 00 b9 e8 03 00 00 f7 ea f7 f9', 'step = cap * dt / 1000'),
    (0x0045459e, '8b 8c 24 78 05 00 00 3b c1 7e 02 8b c1', 'step clamped to the remaining angle'),
    (0x004545ba, 'e8 31 bf 09 00', 'step matrix (0x004f04f0)'),
    (0x004545cb, 'e8 20 d2 09 00', 'p+0x40 multiplied by the step (0x004f17f0)'),
    (0x00454632, '8d 43 2c 50 8d 4b 24 51 8d 43 28 8b ce e8 fc bf 09 00', 'obj+0x24/+0x28/+0x2c = Euler angles of the step'),
    (0x00454647, 'e9 91 4d 00 00', 'jmp 0x004593dd (site B)'),
    (0x0045464c, '33 f6 89 73 24 89 73 28 89 73 2c', 'rates = 0 when the turn stops'),
    # --- slew loop after every command case: carrier hold ---
    (0x0045951f, '8b 46 40 8b d0 c1 ea 14 f6 c2 01 74 2f a9 00 00 08 00 74 28 a9 00 00 00 10 75 13', 'flags 0x100000 & 0x80000, and 0x10000000'),
    (0x0045953a, 'a1 34 6f 60 00 8b 90 18 07 00 00 2b 91 80 01 00 00 79 0e', 'or time < f+0x180'),
    (0x0045954d, '33 c0 89 46 14 89 46 30 89 46 34 89 46 38', 'desired and remainders = 0'),
    (0x0044060d, '39 9c 37 ac 00 00 00 7e 18 8b c5 e8 e3 f6 ff ff 84 c0 74 0d 81 4d 40 00 00 10 00', 'ship creation: record+0xac > 0 and large -> flag 0x100000'),
    (0x004543ad, '81 48 40 00 00 00 10', 'order 5 sets 0x10000000 on the target'),
    # --- integrator: e == 0 rule and remainder consumption ---
    (0x00459980, '8b 43 38 33 ff 3b c7 7e 7a', 'roll: e <= 0 ->'),
    (0x004599aa, '3b c7 0f 84 ea 00 00 00', 'W*k == 0 -> axis skipped, rate kept'),
    (0x004599d9, '8b 43 38 8b 4c 24 30 8b d0 2b d1 85 d2 7f 0a 8b c8 89 4c 24 30 89 7c 24 18 2b c1 89 43 38 8b 44 24 18 89 43 2c',
     'delta >= e -> delta = e, rate = 0'),
    (0x00459a03, '0f 8d 90 00 00 00', 'roll e == 0 -> 0x00459a99'),
    (0x00459a99, '89 7b 2c', 'roll rate = 0'),
    (0x00459b1d, '0f 8d 90 00 00 00', 'pitch e == 0 -> 0x00459bb3'),
    (0x00459bb3, '89 7b 28', 'pitch rate = 0'),
    (0x00459c3b, '0f 8d 92 00 00 00', 'yaw e == 0 -> 0x00459cd3'),
    (0x00459cd3, '89 7b 24', 'yaw rate = 0'),
    # --- integrator: velocity, slip, position ---
    (0x0045a549, '8b 43 70 8b 88 00 01 00 00', 'v_old = p+0x100 (last frame forward * speed)'),
    (0x0045a5bb, '89 04 11', 'p+0x100 = row2 * speed'),
    (0x0045a5c6, '66 83 7b 48 07 0f 85 c0 00 00 00 8b 43 50 66 83 b8 58 02 00 00 05 74 63 39 7b 10 74 5e',
     'class 7, order != 5, speed != 0'),
    (0x0045a5e3, '8b 4c 24 20 8d bc 24 90 00 00 00 8d b4 24 a0 00 00 00 e8 06 66 09 00', 'local = basis * v_old (0x004f0c00)'),
    (0x0045a5fa, '8b 4c 24 20 8b f7 c7 84 24 98 00 00 00 00 00 00 00 e8 90 67 09 00', 'local z = 0, back to world (0x004f0da0)'),
    (0x0045a610, '8b 43 60 8b 4b 64 8b 53 68 03 84 24 90 00 00 00', 'slip += lateral part'),
    (0x0045a69a, '8b 4b 60 03 88 00 01 00 00', 'v = slip + p+0x100'),
    (0x0045a6e1, 'c7 44 24 24 e8 03 00 00 e8 32 7f fb ff', 'd = v * dt / 1000 (0x00412620)'),
    (0x0045a743, 'be 03 00 00 00 8b 07 85 c0 74 24 b9 d0 07 00 00 2b 4c 24 24', 'per component, if != 0: (2000 - dt)'),
    (0x0045a767, 'b9 d0 07 00 00 f7 ea f7 f9 89 07', '/ 2000, store'),
    (0x004f0c31, '8b 41 04', '0x004f0c00: row0[1] * v[1] (rows)'),
    (0x004f0dd1, '8b 41 10', '0x004f0da0: row1[0] * l[1] (transpose)'),
    # --- other slip writers ---
    (0x0045e6de, '8b 53 70 81 c2 00 01 00 00 8d 4b 60 52', 'collision: vA = slipA + velA'),
    (0x0045e709, '8b 86 a0 00 00 00 8b bb a0 00 00 00', 'collision: weights from obj+0xa0'),
    (0x0045e748, 'f6 43 44 80', 'collision: [A+0x44] & 0x80 -> A unchanged'),
    (0x0045e775, '8d 43 60 e8 d3 ff fa ff', 'collision: slipA += impulse'),
    (0x0045e783, '05 00 01 00 00 33 c9 89 08 89 48 04 89 48 08', 'collision: p+0x100 of A = 0'),
    (0x0045e7c1, '83 c0 60 e8 87 ff fa ff', 'collision: slipB += impulse'),
    (0x00457f29, '89 4b 60', 'order 0x1e (undock): slip = carrier slip + velocity'),
    (0x004591f4, '89 4b 60', 'order 0x20 (glide): slip = glide vector'),
    (0x0046326a, '89 70 60 89 50 64 89 48 68', 'SA_SetGlide (case 0x46): slip = script vector'),
    # --- order 8 (0x004551fe) ---
    (0x004551fe, '8b 15 34 6f 60 00 8b 8a 18 07 00 00 2b 8e 14 02 00 00 78 2a', 'time - f+0x214 (deadline) < 0 -> launch'),
    (0x00455212, '8b 4b 50 81 63 40 ff fb ff ff 33 c0 66 89 81 58 02 00 00', 'deadline: flag 0x400 cleared, order 0'),
    (0x0045523c, '83 be 24 02 00 00 00 0f 84 94 41 00 00', 'no gate -> nothing'),
    (0x00455252, '81 48 40 00 00 00 02 81 4b 40 00 04 00 00 8d 81 84 03 00 00 8b c8 0f af c8', 'u = t - deadline + 900, u^2'),
    (0x0045526f, 'b8 e8 67 19 00 8b 54 24 10 b9 40 42 0f 00 f7 ea f7 f9', 's = u^2 * 1665000 / 1000000 (P5 window)'),
    (0x0045528e, '8b 40 70 8b 48 60', 'gate p+0x60 = row 2 (axis)'),
    (0x004552ce, 'e8 ad 94 fb ff', 'axis * s (0x0040e780, 16.16)'),
    (0x0045530c, 'e8 0f 94 fb ff 8b 10 8b 4b 70 89 51 30', 'p+0x30 = f+0x240 + axis * s'),
    (0x0045532e, '33 c0 89 43 14 89 43 10 e9 a2 40 00 00', 'desired = speed = 0'),
    # --- order 0x21 (0x0045533b, warp-in): the mirror law ---
    (0x004553cb, '83 be 24 02 00 00 00 0f 84 05 40 00 00 81 4b 40 00 04 00 00 f7 d8 8b c8 0f af c8', 'u = deadline - t, u^2'),
    (0x0045537c, '8b 43 50 8b 4b 70 8b 90 40 02 00 00 89 51 30', 'at the deadline p+0x30 = f+0x240'),
]
for va, hx, what in SITES:
    want = bytes.fromhex(hx.replace(' ', ''))
    got = rd(va, len(want))
    if got != want:
        fails.append('bytes 0x%08x %s: got %s' % (va, what, got.hex(' ')))

sha = hashlib.sha256(data).hexdigest()
print('sha256', sha, 'OK' if sha == SHA256 else 'MISMATCH')
if sha != SHA256:
    fails.append('sha256')

idx = rd(0x00459694, 0x25)
for word, target in ((5, 0x00453fc2), (8, 0x004551fe), (0x21, 0x0045533b)):
    t = struct.unpack('<I', rd(0x0045963c + 4 * idx[word], 4))[0]
    print('command word 0x%x -> 0x%08x' % (word, t))
    if t != target:
        fails.append('jump table word 0x%x' % word)

try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    starts = set()
    for a, e in ((0x00453fc2, 0x004549c2), (0x004551fe, 0x00455486), (0x004593dd, 0x004595c0), (0x004596e0, 0x0045a7a0),
                 (0x0045e6cb, 0x0045e800), (0x00440604, 0x00440630), (0x00457f0e, 0x00457f40), (0x004591d8, 0x00459210),
                 (0x00463261, 0x00463278), (0x004f0c00, 0x004f0c40), (0x004f0da0, 0x004f0de0)):
        for ins in md.disasm(rd(a, e - a), a):
            starts.add(ins.address)
    for va, hx, what in SITES:
        if va not in starts:
            fails.append('not an instruction start in the sweep: 0x%08x %s' % (va, what))
    print('instruction starts checked:', len(SITES))
except ImportError:
    fails.append('capstone missing')

print('byte strings checked:', len(SITES))
for f in fails:
    print('FAIL', f)
print('RESULT', 'PASS' if not fails else 'FAIL')
