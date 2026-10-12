#!/usr/bin/env python3
"""Re-read every EXE byte string and table entry quoted in docs/reverse-engineering/ship-navigation-ai.md
through the PE section map of the installed X3AP.exe, and print RESULT PASS only if all match.

    python3 -I verification/results/ship-navigation-ai/check_sites.py [path/to/X3AP.exe] > check_sites_out.txt
"""
import hashlib, struct, sys

EXE = sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3/X3AP.exe'
SHA = 'fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab'
d = open(EXE, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec, opt = struct.unpack_from('<H', d, pe + 6)[0], struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
secs = [struct.unpack_from('<IIII', d, pe + 24 + opt + 40 * i + 8) for i in range(nsec)]


def off(va):
    r = va - base
    for vs, sva, rs, ra in secs:
        if sva <= r < sva + max(vs, rs):
            return ra + r - sva
    raise ValueError(hex(va))


def rd(va, n): o = off(va); return d[o:o + n]
def u32(va): return struct.unpack('<I', rd(va, 4))[0]
def cstr(va): o = off(va); return d[o:d.index(b'\0', o)].decode('latin1')


BYTES = [  # (VA, expected bytes, claim)
    (0x0043a38e, '56 e8 bc 2e 02 00 56 e8 36 87 01 00', 'per-sector driver: collide 0x45d250 then simulate 0x452ad0'),
    (0x004344c7, 'c7 04 c8 30 06 46 00', 'SA_ module record +0x30 = dispatcher 0x460630'),
    (0x004344d4, 'c7 44 d0 34 d0 04 46 00', 'SA_ module record +0x34 = completion callback 0x4604d0'),
    (0x004344f6, 'c7 44 c8 40 f0 a7 57 00', 'SA_ module record +0x40 = name table 0x57a7f0'),
    (0x00460630, '55 8b ec 83 e4 f0', 'SA dispatcher prologue'),
    (0x0046065a, '81 fb 79 01 00 00', 'SA dispatcher: cmp cmd, 0x179'),
    (0x0046067a, 'ff 24 9d f0 90 46 00', 'SA dispatcher: jmp [cmd*4+0x4690f0]'),
    (0x00462352, '66 c7 82 58 02 00 00 19 00', 'SA_MovePos: ship+0x258 = 0x19'),
    (0x00462361, '89 91 60 02 00 00', 'SA_MovePos: ship+0x260 = arg4 (precision)'),
    (0x0046236d, '89 91 40 02 00 00', 'SA_MovePos: ship+0x240 = x'),
    (0x00462379, '89 91 44 02 00 00', 'SA_MovePos: ship+0x244 = y'),
    (0x00462385, '89 91 48 02 00 00', 'SA_MovePos: ship+0x248 = z'),
    (0x0046238e, '89 48 7c', 'SA_MovePos: obj+0x7c = module slot'),
    (0x00462391, '89 b0 80 00 00 00', 'SA_MovePos: obj+0x80 = task id'),
    (0x0046108c, '66 c7 82 58 02 00 00 12 00', 'SA_AvoidObjectCollision: ship+0x258 = 0x12'),
    (0x00461106, '05 f0 49 02 00', 'SA_AvoidObjectCollision: default radius obj+0xa4 + 150000'),
    (0x0046110e, '89 81 60 02 00 00', 'SA_AvoidObjectCollision: ship+0x260 = radius'),
    (0x00461f30, '66 c7 81 58 02 00 00 00 00', 'SA_StopObject: ship+0x258 = 0'),
    (0x00460510, '66 c7 80 84 00 00 00 01 00', 'completion callback: task+0x84 = 1'),
    (0x00460560, '8b 46 7c', 'completion notify 0x460560 reads obj+0x7c (ESI = obj)'),
    (0x00453ce0, '0f bf 86 58 02 00 00', 'order interpreter: movsx eax, word [ship+0x258]'),
    (0x00453ce7, '83 f8 24', 'order interpreter: cmp eax, 0x24'),
    (0x00453cf0, '0f b6 90 94 96 45 00', 'order interpreter: byte table 0x459694'),
    (0x00453cf7, 'ff 24 95 3c 96 45 00', 'order interpreter: jump table 0x45963c'),
    (0x00456527, 'e8 94 67 ff ff', 'order 0x19: call 0x44ccc0'),
    (0x00456530, 'e8 eb 6a ff ff', 'order 0x19: call 0x44d020'),
    (0x00456576, '3b b0 60 02 00 00', 'order 0x19: arrival when dist <= ship+0x260'),
    (0x00456592, 'e8 c9 9f 00 00', 'order 0x19: call 0x460560 (wake task)'),
    (0x00456798, '81 c1 c8 00 00 00', 'order 0x12: re-plan every 200 ms'),
    (0x00454b48, '3d b8 56 00 00', 'order 4: arrival radius floor 0x56b8'),
    (0x00454b5d, '3d 98 b1 01 00', 'order 4: arrival radius cap 0x1b198'),
    (0x004542d2, '3d f8 24 01 00', 'order 5: approach speed cap 75000 (large classes)'),
    (0x004542f1, '3d 50 c3 00 00', 'order 5: approach speed cap 50000 (other classes)'),
    (0x004404a1, '6b c9 32 89 8a 0c 02 00 00', 'ship init: ship+0x20c = type+0x50 * 50'),
    (0x0044ccc0, '83 ec 18 8b 02', 'aim-at-point 0x44ccc0 entry (EDX = point)'),
    (0x0044d011, 'c2 04 00', '0x44ccc0 ret 4'),
    (0x0044d020, '55 8b ec 83 e4 f0', 'steer-to-point 0x44d020 entry'),
    (0x0044d107, 'e8 b4 fb ff ff', '0x44d020 tail call 0x44ccc0'),
    (0x0044d112, 'c2 08 00', '0x44d020 ret 8'),
    (0x004593dd, '8b 1b 83 3b 00 0f 85 78 97 ff ff', 'object-loop continue (post-order join)'),
    (0x0045d280, '81 60 40 ff cf 9f ff', 'collide L0 clears obj+0x40 bits 0x1000/0x2000'),
    (0x0045d298, '89 bb e0 01 00 00', 'collide L0 clears ship+0x1e0 obstacle count'),
    (0x0045d51c, 'ba 00 00 03 00', 'class-7 box gate: r_sum * 3'),
    (0x0045d538, '05 90 d0 03 00', 'class-7 box gate: + 250000'),
    (0x0045d844, 'e8 07 0c ff ff', 'collide: proximity 0x44e450(A, B, d)'),
    (0x0045d867, 'e8 e4 0b ff ff', 'collide: proximity 0x44e450(B, A, d)'),
    (0x0044e450, '53 8b 5c 24 08', 'proximity 0x44e450 entry'),
    (0x0044e52b, '81 4b 40 00 10 00 00', 'proximity: obj+0x40 |= 0x1000 (warn)'),
    (0x0044e552, '83 f8 05', 'proximity: obstacle list cap 5'),
    (0x0044e56c, '81 4b 40 00 20 00 00', 'proximity: obj+0x40 |= 0x2000 (listed)'),
    (0x0044e581, 'c2 0c 00', '0x44e450 ret 0xc'),
    (0x0044dd90, '55 8b ec 83 e4 f0', 'warn test 0x44dd90 entry'),
    (0x0044ded2, 'c2 10 00', '0x44dd90 ret 0x10'),
    (0x0044ded9, 'ba 33 33 01 00', 'warn test: 1.2 * r_sum (0x13333)'),
    (0x0044df25, '81 fe 98 3a 00 00', 'warn test: margin floor 15000'),
    (0x0044df34, '81 fe 20 a1 07 00', 'warn test: margin cap 500000'),
    (0x0044e07c, '3d cc cc 00 00', 'warn test: B speed term only when A heads at B (cos > 0.8)'),
    (0x0045e05a, '66 83 ba 58 02 00 00 12', 'collide L3: no CollisionWarn while order 0x12'),
    (0x0045e079, '68 74 fd 55 00', 'collide L3: push "CollisionWarn"'),
    (0x0045e089, 'e8 32 14 04 00', 'collide L3: call 0x49f4c0'),
    (0x004637df, '8b 40 40', 'SA_GetFlags returns obj+0x40'),
    (0x00463e88, '81 48 40 00 00 08 00', 'SA_SetAutopilot: obj+0x40 |= 0x80000'),
    (0x0044d5de, 'c7 44 24 10 33 b3 00 00', 'gate test: plane window 0.7 * gate radius'),
    (0x0044d5e8, 'c7 44 24 10 d7 23 00 00', 'gate test: plane window 0.14 * radius (gate subtypes 5..8)'),
    (0x0044d7d9, '3d 20 53 14 00', 'dock test: range floor 0x145320 (1,331,000)'),
    (0x0044d89b, '05 a8 61 00 00', 'dock test: port reach obj+0xa4 + 25000'),
    (0x00452ae7, '3d e8 03 00 00', 'sector_simulate: dt clamp 1000 ms'),
]
SA_IDS = {9: ('SA_FlyObjectIntoContainer', 0x46082e), 10: ('SA_FlyObjectBeforeContainer', 0x460cae),
          12: ('SA_FlyObjectBeforeContainerPos', 0x46533c), 13: ('SA_ClearObjectFromHangar', 0x46117e),
          14: ('SA_WarpObjectAway', 0x46126a), 15: ('SA_AvoidObjectCollision', 0x46104c),
          17: ('SA_AttackObject', 0x4614da), 18: ('SA_AttackObjectBig', 0x4616e0), 19: ('SA_DefensiveMove', 0x4617cf),
          20: ('SA_EscortObject', 0x4618a6), 22: ('SA_TurnToObject', 0x461fdd), 26: ('SA_Idle', 0x46274e),
          27: ('SA_FollowObject', 0x46196f), 28: ('SA_FollowObjectByCut', 0x461a13), 29: ('SA_RotateToAngles', 0x4653d1),
          30: ('SA_MovePos', 0x46232c), 31: ('SA_MovePosContinue', 0x46239c), 32: ('SA_GlidePos', 0x4656f3),
          33: ('SA_WarpObjectIn', 0x465b22), 64: ('SA_SetSpeed', 0x464226), 66: ('SA_SetDesiredSpeed', 0x4641c6),
          82: ('SA_SetMaxAvoidShield', 0x463689), 250: ('SA_StopObject', 0x461e89), 261: ('SA_CollisionObject', 0x46101f),
          262: ('SA_GetCollisionObjects', 0x463a7e), 281: ('SA_SetPlayerLand', 0x463bc4), 288: ('SA_SetAutopilot', 0x463e6e),
          324: ('SA_SetDestination', 0x465a03), 328: ('SA_SetSpeedLimit', 0x465d1a), 329: ('SA_SetMaxSpeedOverride', 0x465d4e),
          376: ('SA_SetFlightMode', 0x468db4)}
ORDERS = {0x00: 0x458f8e, 0x04: 0x4549c2, 0x05: 0x453fc2, 0x06: 0x4554af, 0x08: 0x4551fe, 0x0a: 0x455486,
          0x10: 0x455e8c, 0x11: 0x455e8c, 0x12: 0x456722, 0x14: 0x45820d, 0x15: 0x458aea, 0x16: 0x458e93,
          0x18: 0x455e8c, 0x19: 0x4564cd, 0x1b: 0x45512c, 0x1d: 0x453cfe, 0x1e: 0x457bc3, 0x1f: 0x45920e,
          0x20: 0x459141, 0x21: 0x45533b, 0x22: 0x455696, 0x23: 0x45659c, 0x24: 0x455ae5}
SG_SH = ['SG_SH_TL', 'SG_SH_TS', 'SG_SH_M0', 'SG_SH_M1', 'SG_SH_M2', 'SG_SH_M3', 'SG_SH_M4', 'SG_SH_M5', 'SG_SH_TL_P',
         'SG_SH_TS_P', 'SG_SH_GO', 'SG_SH_M6', 'SG_SH_TP', 'SG_SH_M7', 'SG_SH_TM', 'SG_SH_M8']

fails = 0
sha = hashlib.sha256(d).hexdigest()
print('X3AP.exe', len(d), 'B sha256', sha, 'MATCH' if sha == SHA else 'MISMATCH'); fails += sha != SHA
for va, exp, claim in BYTES:
    got = rd(va, len(bytes.fromhex(exp))).hex(' ')
    ok = got == exp; fails += not ok
    print(f'{"ok  " if ok else "FAIL"} {va:#010x} {got:34s} {claim}')
for i, (name, target) in sorted(SA_IDS.items()):
    n, t = cstr(u32(0x0057a7f0 + 4 * i)), u32(0x004690f0 + 4 * i)
    ok = n == name and t == target; fails += not ok
    print(f'{"ok  " if ok else "FAIL"} SA id {i:#05x} name {n} case {t:#x}')
for code, target in sorted(ORDERS.items()):
    t = u32(0x0045963c + 4 * rd(0x00459694 + code, 1)[0])
    ok = t == target; fails += not ok
    print(f'{"ok  " if ok else "FAIL"} order {code:#04x} handler {t:#x}')
for i, name in enumerate(SG_SH):
    p, v = struct.unpack('<II', rd(0x00552758 + 8 * i, 8))
    ok = cstr(p) == name and v == i; fails += not ok
    print(f'{"ok  " if ok else "FAIL"} type+0x14 code {v:2d} = {cstr(p)}')
s = cstr(0x0055fd74); ok = s == 'CollisionWarn'; fails += not ok
print(f'{"ok  " if ok else "FAIL"} string 0x55fd74 = {s}')
print('RESULT', 'PASS' if fails == 0 else f'FAIL ({fails})')
