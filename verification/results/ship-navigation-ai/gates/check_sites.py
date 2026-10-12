#!/usr/bin/env python3
"""Re-read every EXE byte string quoted in docs/reverse-engineering/ship-navigation-ai.md section 10 (gate transit)
through the PE section map of the installed X3AP.exe, check that no branch lands inside the proposed hook windows,
and print RESULT PASS only if all match.

    python3 -I verification/results/ship-navigation-ai/gates/check_sites.py [path/to/X3AP.exe] > check_sites_out.txt
"""
import hashlib, struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

EXE = sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3/X3AP.exe'
SHA = 'fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab'
d = open(EXE, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec, opt = struct.unpack_from('<H', d, pe + 6)[0], struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
secs = [(d[pe + 24 + opt + 40 * i:pe + 24 + opt + 40 * i + 8].rstrip(b'\0').decode(),) +
        struct.unpack_from('<IIII', d, pe + 24 + opt + 40 * i + 8) for i in range(nsec)]


def off(va):
    r = va - base
    for _n, vs, sva, rs, ra in secs:
        if sva <= r < sva + max(vs, rs):
            return ra + r - sva
    raise ValueError(hex(va))


def rd(va, n): o = off(va); return d[o:o + n]


BYTES = [  # (VA, expected bytes, claim)
    # gate object init (class 0x12 table 0x607000): approach and pass vectors in [gate+0x50]+0x190/+0x1a0, x r_gate
    (0x00440e0d, '8b 0d 00 70 60 00', 'gate init: class-0x12 record table *0x607000'),
    (0x00440e37, '0f bf 45 4a 83 c0 fb 83 f8 03', 'gate init: subtype - 5 <= 3 (subtypes 5..8)'),
    (0x00440e44, '89 98 90 01 00 00', 'gate init: +0x190 (pass x) = ebx (0)'),
    (0x00440e4f, 'c7 81 94 01 00 00 34 f3 ff ff', 'gate init: +0x194 (pass y) = -0.05 (subtypes 5..8)'),
    (0x00440e5b, 'c7 81 94 01 00 00 34 73 ff ff', 'gate init: +0x194 (pass y) = -0.55 (other subtypes)'),
    (0x00440e68, 'c7 82 98 01 00 00 71 fd ff ff', 'gate init: +0x198 (pass z) = -0.01'),
    (0x00440e75, '89 98 a0 01 00 00', 'gate init: +0x1a0 (approach x) = ebx (0)'),
    (0x00440e7e, 'c7 81 a4 01 00 00 34 33 ff ff', 'gate init: +0x1a4 (approach y) = -0.8'),
    (0x00440e8b, 'c7 82 a8 01 00 00 00 00 ff ff', 'gate init: +0x1a8 (approach z) = -1.0'),
    (0x00440e95, '81 4d 40 00 00 10 00', 'gate init: [gate+0x40] |= 0x100000'),
    (0x0043ffc4, '33 db', 'gate init function: xor ebx, ebx (the zero register of the stores above)'),
    # SA_FlyObjectBeforeContainer (id 0x0a, case 0x460cae)
    (0x00460d16, '8b 42 40 c1 e8 14 a8 01', 'FlyBefore: container flag 0x100000 test'),
    (0x00460d24, '83 7c 24 0c 00 0f 8c a6 00 00 00', 'FlyBefore: port < 0 -> approach-vector path 0x460dd5'),
    (0x00460dd9, '8b 48 70 8b 70 50 83 c1 40 81 c6 a0 01 00 00', 'FlyBefore: rotate [cont+0x50]+0x1a0 by the container frame'),
    (0x00460df8, '81 c6 a4 00 00 00', 'FlyBefore: scale by the container radius [cont+0xa4]'),
    (0x00460e6b, '66 c7 81 58 02 00 00 04 00', 'FlyBefore: order 4'),
    # SA_FlyObjectIntoContainer (id 0x09, case 0x46082e), gate branch
    (0x004608cb, '8b 56 40 c1 ea 14 f6 c2 01', 'FlyInto: container flag 0x100000 test'),
    (0x004608de, '66 3d 12 00 0f 85 70 01 00 00', 'FlyInto: class 0x12 (gate) branch'),
    (0x004608ea, 'e8 61 f4 fd ff 84 c0 0f 85 23 03 00 00', 'FlyInto: capital (0x43fd50) -> gate origin 0x460c1a'),
    (0x004608f7, '68 00 40 00 00 e8 7f 25 09 00 2d 00 20 00 00', 'FlyInto: theta = rand(0x4000) - 0x2000 (+-45 deg)'),
    (0x004609b2, '8b 88 90 01 00 00', 'FlyInto: reads the pass vector [gate+0x50]+0x190'),
    (0x004609db, 'e8 50 ee fc ff f7 d8', 'FlyInto: z = -MulFix(sin theta, y0)'),
    (0x004609e8, 'e8 43 ee fc ff', 'FlyInto: y = MulFix(cos theta, y0)'),
    (0x004609ed, '8d b7 a4 00 00 00', 'FlyInto: scale by r_gate [gate+0xa4]'),
    (0x00460c1a, '8b 46 70 83 c0 30', 'FlyInto: point = container position (capital / no flag)'),
    (0x00460c72, '66 c7 81 58 02 00 00 05 00', 'FlyInto: order 5'),
    # order 4 handler 0x4549c2
    (0x00454a20, '83 b8 50 02 00 00 00', 'order 4: port < 0 -> point from flight+0x240'),
    (0x00454b09, '0f b7 47 48 66 3d 12 00', 'order 4: target class 0x12 (gate)'),
    (0x00454b1a, '8b 87 a4 00 00 00 8d 04 40 99 83 e2 03 03 c2 c1 f8 02', 'order 4: gate arrival radius 3 r_gate / 4'),
    (0x00454b70, '3b f0 7f 2d', 'order 4: arrived when dist <= radius'),
    (0x00454b8d, '6a 01', 'order 4: result 1'),
    (0x00454b91, '89 43 14 89 43 10 e8 c4 b9 00 00', 'order 4: desired = 0, CURRENT SPEED = 0, wake (hook window P2)'),
    (0x00455014, 'e8 17 4a 01 00', 'order 4: brake distance 0x469a30(vmax, vmax, a)'),
    (0x00455052, '68 88 13 00 00 d1 ff 6a 00 e8 c0 aa fd ff', 'order 4: ramp(dist; 0 -> 5000, d_slow -> vmax/2)'),
    (0x0045506d, '81 c1 88 13 00 00', 'order 4: floor container speed + 5000'),
    (0x004550a8, '03 c8 8d 04 41 8d 0c 50', 'order 4: align zone d_slow + 3 r_cont + 2 r_ship'),
    (0x004550bd, '68 f4 01 00 00', 'order 4: align ramp y0 = 500'),
    (0x004550c7, '68 33 b3 00 00 b8 00 00 01 00 e8 4a aa fd ff', 'order 4: align ramp(dot; 0.7 -> 500, 1.0 -> desired)'),
    (0x004550ed, '81 fe 50 c3 00 00', 'order 4: large ships capped at 50000'),
    # order 5 handler 0x453fc2, gate path
    (0x0045430c, 'b8 67 66 66 66 f7 e9 c1 fa 02', 'order 5: |v| / 10'),
    (0x00454323, '8b 82 cc 00 00 00', 'order 5: time factor [*0x606f34+0xcc]'),
    (0x004546e3, '39 4c 24 1c 0f 9e c2', 'order 5: arrived when dist <= (|v|/10 + r)(1 + (tf-1)/4)'),
    (0x00454700, '89 43 14 89 43 10', 'order 5: desired = 0, CURRENT SPEED = 0 (hook window P4)'),
    (0x00454713, '6a 01', 'order 5: result 1'),
    (0x00454745, 'e8 76 85 ff ff', 'order 5: steering call 0x44ccc0'),
    (0x00454897, '3d 33 f3 00 00 7d 1a', 'order 5: aligned when forward . dir >= 0.95'),
    (0x004548a5, 'c7 43 14 f4 01 00 00', 'order 5: desired 500 while misaligned (hook window P3)'),
    (0x004548bf, '66 83 7a 48 12', 'order 5: gate branch of the speed law'),
    (0x004548cf, '81 48 40 00 00 00 02', 'order 5: [gate+0x40] |= 0x2000000'),
    (0x004548f5, 'd1 fe', 'order 5 gate ramp: y0 = vmax / 2'),
    (0x00454903, 'b8 67 66 66 66 f7 ee d1 fa', 'order 5 gate ramp: x0 = r_gate / 5'),
    (0x00454977, '8b c6 8b 4c 24 24 e8 9e b1 fd ff', 'order 5 gate ramp: x1 = r_gate, y1 = vmax'),
    # order 8 handler 0x4551fe
    (0x00455252, '81 48 40 00 00 00 02 81 4b 40 00 04 00 00', 'order 8: gate |= 0x2000000, ship |= 0x400'),
    (0x00455260, '8d 81 84 03 00 00 8b c8 0f af c8', 'order 8: t = elapsed (900 + now - end), t^2'),
    (0x0045526f, 'b8 e8 67 19 00', 'order 8: s = t^2 * 1665000 / ...'),
    (0x00455278, 'b9 40 42 0f 00', 'order 8: ... / 1000000'),
    (0x00455291, '8b 48 60', 'order 8: along gate row 2 (+0x60, the gate axis)'),
    (0x00455330, '89 43 14 89 43 10', 'order 8: desired = speed = 0 every frame'),
    # order 0x1f handler 0x45920e and SA_RotateToAngles case 0x4653d1
    (0x004592d9, '3d 70 fd 00 00 0f 8c 7c 00 00 00', 'order 0x1f: done when forward . target >= 0xfd70 (8.1 deg)'),
    (0x004592ea, '3d 00 00 00 80', 'order 0x1f: no roll target (0x80000000) -> done'),
    (0x004653f4, '83 7d 14 03', 'SA_RotateToAngles: argc <= 3 -> no roll'),
    (0x004653fb, '66 c7 81 58 02 00 00 1f 00', 'SA_RotateToAngles: order 0x1f'),
    (0x00465466, 'c7 81 48 02 00 00 00 00 00 80', 'SA_RotateToAngles: +0x248 = 0x80000000'),
    # order 0x1d (SA_Idle) handler 0x453cfe
    (0x00453e64, '05 f0 ef 10 00', 'order 0x1d: wander point >= 1110000 ahead'),
    (0x00453ec1, 'b8 e0 df 21 00', 'order 0x1d: y in +-1110000'),
    (0x00453eea, '81 84 24 14 03 00 00 40 4b 4c 00', 'order 0x1d: capital y + 5000000'),
    (0x00453fb8, 'd1 f8 89 43 14', 'order 0x1d: desired = vmax / 2'),
]
WINDOWS = [(0x00454b91, 6, 'P2 order-4 arrival writes'), (0x00454700, 6, 'P4 order-5 arrival writes'),
           (0x004548a5, 7, 'P3 order-5 misaligned speed'), (0x004592d9, 5, 'order-0x1f tolerance'),
           (0x0045526f, 5, 'P5 order-8 distance law')]

fails = 0
sha = hashlib.sha256(d).hexdigest()
print('X3AP.exe', len(d), 'B sha256', sha, 'MATCH' if sha == SHA else 'MISMATCH'); fails += sha != SHA
for va, exp, claim in BYTES:
    got = rd(va, len(bytes.fromhex(exp))).hex(' ')
    ok = got == exp; fails += not ok
    print(f'{"ok  " if ok else "FAIL"} {va:#010x} {got:44s} {claim}')
md = Cs(CS_ARCH_X86, CS_MODE_32); md.skipdata = True
inside = {va + k: name for va, n, name in WINDOWS for k in range(1, n)}
starts = {va: name for va, n, name in WINDOWS}
hits, seen_start = [], set()
for n_, vs, sva, rs, ra in secs:
    if n_ != '.text': continue
    for i in md.disasm(d[ra:ra + rs], base + sva):
        if i.address in starts: seen_start.add(i.address)
        if i.mnemonic.startswith('j') or i.mnemonic == 'call':
            try: t = int(i.op_str, 16)
            except ValueError: continue
            if t in inside: hits.append((i.address, t, inside[t]))
for va, n, name in WINDOWS:
    ok = va in seen_start and not any(h[2] == name for h in hits); fails += not ok
    print(f'{"ok  " if ok else "FAIL"} window {va:#010x}+{n}: instruction start in the linear sweep, no direct branch into '
          f'bytes 1..{n - 1} ({name})')
for h in hits: print('     branch', hex(h[0]), '->', hex(h[1]), h[2])
print('RESULT', 'PASS' if fails == 0 else f'FAIL ({fails})', f'({len(BYTES) + len(WINDOWS) + 1} checks)')
