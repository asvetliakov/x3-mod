#!/usr/bin/env python3
"""Re-read every EXE byte string quoted in docs/reverse-engineering/ship-navigation-ai.md section 10.10 (the order-5
transfer test, the arrival placement path through SA_StartInHangar and the post-pass launch 0x444170, order 0x21 and
0x1e at a gate, the arrival signals), check the SA/order jump-table entries and the two doubles of the exit fan, check
that no direct branch lands inside the proposed launch window 0x444606, and print RESULT PASS only if all match.

    python3 -I verification/results/ship-navigation-ai/gates/arrival_sites.py [path/to/X3AP.exe] > arrival_sites_out.txt
    python3 -I verification/results/ship-navigation-ai/gates/arrival_sites.py --log path/to/x3m.log   # site-W counter sums

With --log it sums the per-interval counters gate_warp_calls (site W entries), gate_p4 (order-5 gate arrivals) and
gate_p5 (order-8 launch frames) over the ship_steering_summary rows of an x3m.log instead.
"""
import hashlib, re, struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

if len(sys.argv) > 2 and sys.argv[1] == '--log':
    tot = {}
    with open(sys.argv[2], errors='replace') as f:
        for line in f:
            if line.startswith('ship_steering_summary'):
                tot['rows'] = tot.get('rows', 0) + 1
                for k, v in re.findall(r'\b(gate_(?:warp_calls|exit_offsets|warp_skipped|p4|p5))=(\d+)', line):
                    tot[k] = tot.get(k, 0) + int(v)
    print(sys.argv[2], tot)
    sys.exit(0)

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
    (0x0045413e, '8d b7 40 02 00 00 83 c1 30', 'order 5: esi = &f+0x240 (stored order point); ecx = &phys+0x30 (ship centre)'),
    (0x0045414f, 'e8 3c e3 fb ff', 'order 5: f+0x240 - phys+0x30 (0x412490: out = [arg] - [ecx])'),
    (0x00454182, 'e8 f9 59 01 00 89 44 24 1c', 'order 5: [esp+0x1c] = |ship centre - f+0x240| (0x469b80 length)'),
    (0x00454192, '89 8c 24 a0 04 00 00', 'order 5: leaf target copy [esp+0x4a0] = f+0x240 (x)'),
    (0x004541b4, '66 83 78 48 12', 'order 5: target class 0x12 (gate) branch'),
    (0x00454303, '8b 4b 10', 'order 5 arrival: current speed [obj+0x10]'),
    (0x0045430c, 'b8 67 66 66 66 f7 e9', 'order 5 arrival: |v| / 10 (magic 0x66666667, sar 2)'),
    (0x00454323, '8b 82 cc 00 00 00', 'order 5 arrival: tf = [*0x606f34+0xcc]'),
    (0x00454329, '03 8b a4 00 00 00', 'order 5 arrival: + r_ship [obj+0xa4]'),
    (0x0045432f, '2d 00 00 01 00', 'order 5 arrival: (tf - 0x10000)'),
    (0x0045433a, 'c1 f8 02', 'order 5 arrival: / 4'),
    (0x00454384, '66 83 78 48 12 0f 84 52 03 00 00', 'order 5: gate target -> arrival compare at 0x4546e1 every frame'),
    (0x004546e3, '39 4c 24 1c 0f 9e c2', 'order 5 arrival: dist <= (|v|/10 + r_ship)(1 + (tf-1)/4)'),
    (0x00454733, '8d 94 24 a4 04 00 00', 'order 5: leaf argument edx = &[esp+0x4a0] (after push ebx)'),
    (0x00454745, 'e8 76 85 ff ff', 'order 5: steering leaf 0x44ccc0'),
    (0x0044d4fe, '66 3d 12 00', 'CanWarp 0x44d4c0: other object class 0x12 (gate)'),
    (0x0044d50c, 'f6 c2 01', 'CanWarp: [ship+0x40] bit 2 (after shr 1) ...'),
    (0x0044d513, 'e8 68 2a ff ff', 'CanWarp: ... or 0x43ff80 (manual player control)'),
    (0x0044d523, '66 83 b8 58 02 00 00 21', 'CanWarp: not on order 0x21'),
    (0x0044d531, '39 4d 10', 'CanWarp: centre distance < r_gate'),
    (0x0044d5de, 'c7 44 24 10 33 b3 00 00', 'CanWarp: k = 0.7 (0xb333)'),
    (0x0044d5e8, 'c7 44 24 10 d7 23 00 00', 'CanWarp: k = 0.14 (0x23d7) for subtypes 5..8'),
    (0x0044d606, '39 45 10', 'CanWarp: centre distance < k r_gate'),
    (0x0044d618, '8b 82 98 01 00 00', 'CanWarp: pass-vector z [gate.f+0x198] (-0.01) x r_gate'),
    (0x0044d64a, 'ba 66 e6 00 00', 'CanWarp: x 0.9 (0xe666)'),
    (0x0044d6b8, '81 c2 00 01 00 00', 'CanWarp: velocity = phys+0x100 + slip [obj+0x60]'),
    (0x0044d703, '8d 04 40', 'CanWarp: 3 r_ship (then sar 2: 0.75 r_ship)'),
    (0x0044d715, 'b8 03 00 00 00', 'CanWarp: return 3'),
    (0x0043ff83, 'a9 00 00 04 00', '0x43ff80: [obj+0x40] & 0x40000 ...'),
    (0x0043ff8a, 'a9 00 00 08 00', '0x43ff80: ... and not 0x80000 (autopilot)'),
    (0x0044ada1, '81 4e 40 02 00 04 00', 'SA_SetAsPlayerObject(ship,1) core: [obj+0x40] |= 0x40002 (bit 2 = player object)'),
    (0x0044ae93, '83 66 40 fd', 'SA_SetAsPlayerObject(ship,0) core: [obj+0x40] &= ~2'),
    (0x00463e1e, '6a 01 e8 6b 6f fe ff', 'SA_SetAsPlayerObject case -> 0x44ad90(1)'),
    (0x00462720, 'e8 fb 11 fe ff', 'SA_StartInHangar case: core 0x443920'),
    (0x0046272e, '83 f8 01 0f 84 5b e7 ff ff', 'SA_StartInHangar: core 1 -> 0x460e92 (task waits, no result)'),
    (0x00460e99, '89 4b 7c 89 93 80 00 00 00', '0x460e92: waiting slot [obj+0x7c], task [obj+0x80]'),
    (0x00462746, '6a 0a', 'SA_StartInHangar: core 0 -> result 10'),
    (0x00443a86, '66 83 7f 48 12', 'StartInHangar core: container class 0x12 (gate)'),
    (0x00443a93, 'e8 b8 c2 ff ff', 'StartInHangar core: capital (0x43fd50) skips the warp-out slots'),
    (0x00443aa8, 'b9 06 00 00 00', 'StartInHangar core: 6 warp-out slots ...'),
    (0x00443ab3, 'b9 07 00 00 00', 'StartInHangar core: ... 7 for the player ship ([0x60850c]+0x38)'),
    (0x00443b0b, '33 c0', 'StartInHangar core: no free slot -> 0'),
    (0x00443b3d, '89 9c 81 dc 01 00 00', 'StartInHangar core: occupant [gate.f+0x1dc+4i] = ship'),
    (0x00443b53, '81 c1 20 4e 00 00 89 8c 82 c0 01 00 00', 'StartInHangar core: expiry [gate.f+0x1c0+4i] = now + 20000'),
    (0x00444157, '0d 80 00 00 00 89 43 40', 'StartInHangar core (gate tail): [ship+0x40] |= 0x80'),
    (0x00444160, 'b8 01 00 00 00', 'StartInHangar core: return 1'),
    (0x0043a395, 'e8 36 87 01 00 56', 'sector update: order interpreter 0x452ad0, then post pass 0x45b720'),
    (0x00403aff, 'e8 6c bc 09 00', 'main loop: KC scheduler 0x49f770 (last of three calls) ...'),
    (0x00403b17, 'e8 44 68 03 00', 'main loop: ... then the sector update 0x43a360'),
    (0x0045b777, '66 83 7e 48 12', 'post pass walk 1: class 0x12 (gate)'),
    (0x0045b785, '8b 46 50 8b 78 54', 'post pass: the gate contained class-7 list [gate.f+0x54]'),
    (0x0045b790, '84 5f 40', 'post pass: contained ship with [ship+0x40] & 0x80 ...'),
    (0x0045b797, 'e8 d4 89 fe ff', 'post pass: ... -> 0x444170 (launch from the gate)'),
    (0x0045b7c7, '2b fa 85 ff 7f 0c 89 11', 'post pass: expired warp-out slot -> stamp now ...'),
    (0x0045b7d2, 'c7 04 08 00 00 00 00', 'post pass: ... occupant 0'),
    (0x00444187, 'b8 10 27 00 00', 'launch 0x444170: gate -> 10000'),
    (0x004441c3, '89 90 80 01 00 00', 'launch: [gate.f+0x180] = now + 10000'),
    (0x004441ce, '66 89 b9 58 02 00 00', 'launch: order 0'),
    (0x00444311, 'e8 3a ba ff ff', 'launch: capital flag 0x43fd50'),
    (0x00444335, '8d 8f dc 01 00 00', 'launch: search the ship in the occupants [gate.f+0x1dc..]'),
    (0x0044434a, '83 f8 07', 'launch: 7 entries'),
    (0x00444358, '83 c0 fd', 'launch: phi index = slot - 3'),
    (0x00444363, 'dc 0d b8 57 56 00 dc 0d c0 54 56 00', 'launch: x (1/24) x 65536 (doubles at 0x5657b8, 0x5654c0): 15 deg per slot'),
    (0x0044442c, '8b 97 90 01 00 00', 'launch: pass vector [gate.f+0x190..] (0, y0, -0.01)'),
    (0x00444484, 'f7 d8', 'launch: y = -(cos phi . y0)'),
    (0x004444a9, '2b 8b a4 00 00 00', 'launch: z = z0 r - r_ship'),
    (0x004444c9, 'e8 d2 c8 0a 00', 'launch: rotate by the gate frame (0x4f0da0, rows [gate.p+0x40])'),
    (0x004444f2, '89 08', 'launch: phys+0x30 = P (non-capital)'),
    (0x00444519, 'c7 44 24 24 5c 2b 00 00', 'launch capital: y = 11100 units (22.2 m), x = 0'),
    (0x00444542, '2b 83 a4 00 00 00', 'launch capital: z = z0 r - r_ship'),
    (0x00444581, '89 08', 'launch: phys+0x30 = P (capital)'),
    (0x00444594, '68 00 80 00 00', 'launch: relative angles (0x8000, 0, 0) (yaw 180 deg)'),
    (0x0044459d, 'e8 ce bc 0a 00', 'launch: angle matrix 0x4f0270'),
    (0x004445b4, 'e8 37 d2 0a 00', 'launch: x gate matrix -> ship matrix (0x4f17f0)'),
    (0x004445d5, '66 c7 82 58 02 00 00 21 00', 'launch: order 0x21'),
    (0x004445fa, '81 c2 84 03 00 00', 'launch: deadline now + 900'),
    (0x00444606, '8b 43 70 8b 4b 50', 'launch: PROPOSED SITE: mov eax,[ebx+0x70] | mov ecx,[ebx+0x50] (6 B) before the copy P -> f+0x240'),
    (0x00444612, '89 91 40 02 00 00', 'launch: f+0x240 = phys+0x30'),
    (0x00444630, '81 4b 40 00 04 00 00', 'launch: [ship+0x40] |= 0x400'),
    (0x0044463a, 'b8 e8 67 19 00 ba 10 5c 0c 00', 'launch: 1665000 x 810000 / 10^6 = 1348650 units along gate row 2'),
    (0x004446c8, '89 ba 24 02 00 00', 'launch: f+0x224 = gate'),
    (0x004448a5, '66 39 77 48', 'launch tail: container class 0x12 ...'),
    (0x004448b3, '25 a0 15 ff ff 05 c0 d4 01 00', 'launch tail: speed = capital ? 60000 : 120000'),
    (0x004448c0, 'c7 43 14 20 4e 00 00', 'launch tail: desired 20000'),
    (0x004448e3, 'e8 28 4c 00 00', 'launch tail: link into the gate sector (0x449510)'),
    (0x004448fc, '81 63 40 7f ff ff ff', 'launch tail: [ship+0x40] &= ~0x80'),
    (0x00444906, 'd1 ea f6 c2 01', 'launch tail: bit 2 (player object) -> nearby-ship pass (capital)'),
    (0x00444a32, '6a 01 51 ff d0', 'launch tail: wake the waiting task with 1'),
    (0x00465b68, '66 39 91 58 02 00 00 75 0c 39 81 24 02 00 00 0f 84 1f 01 00 00', 'SA_WarpObjectIn: order 0x21 already on with f+0x224 == target -> 0x465c9c (no placement, site W skipped)'),
    (0x004553df, 'f7 d8', 'order 0x21: u = deadline - now'),
    (0x0045537c, '8b 43 50 8b 4b 70', 'order 0x21 end: phys+0x30 = f+0x240'),
    (0x00457be9, '66 83 ff 12', 'order 0x1e: container class 0x12'),
    (0x00457c24, 'e8 67 a8 fb ff', 'order 0x1e: ship - gate'),
    (0x00457c5e, 'e8 9d 6b fb ff', 'order 0x1e: dz = (ship - gate) . gate row 2'),
    (0x00457c67, 'e8 94 80 fe ff', 'order 0x1e: large predicate 0x43fd00'),
    (0x00457c7e, 'c1 f8 02', 'order 0x1e: limit r_gate / 4 for non-large'),
    (0x00457c89, '3b d0 7e 0a', 'order 0x1e: |dz| > limit -> done (result 1)'),
    (0x00457d2e, 'e8 8d 4f ff ff', 'order 0x1e: steer at ship + row2 (dz -+ r_gate): lateral kept'),
    (0x00457d48, 'c7 43 10 60 ea 00 00', 'order 0x1e: large: speed 60000 every frame'),
    (0x00457d5c, 'c7 43 10 c0 d4 01 00', 'order 0x1e: other: speed 120000 every frame'),
    (0x004611ce, '66 83 f9 12', 'SA_ClearObjectFromHangar: gate container allowed'),
    (0x00461236, '7c 1d', 'SA_ClearObjectFromHangar: port < 0 -> no exit point'),
    (0x0046243c, 'e8 6f 73 fe ff', 'SA_MoveObjectIntoContainer case -> 0x4497b0'),
    (0x004498f9, '89 73 54', '0x4497b0: [obj+0x54] = container (no position write)'),
    (0x00465acc, '66 83 78 48 12', 'SA_IsWarpOutSlotAvailable: class 0x12'),
    (0x00465ae4, '05 c0 01 00 00', 'SA_IsWarpOutSlotAvailable: slots [gate.f+0x1c0..]'),
    (0x00465b00, '83 f9 06', 'SA_IsWarpOutSlotAvailable: 6 slots'),
]
SA_CASES = {0x0d: 0x0046117e, 0x19: 0x004626e2, 0x21: 0x00465b22, 0x5c: 0x0046240c, 0x11e: 0x00463de6, 0x145: 0x00465ab4}
ORDERS = {0x05: 0x00453fc2, 0x08: 0x004551fe, 0x1e: 0x00457bc3, 0x21: 0x0045533b}
DOUBLES = {0x005657b8: 1 / 24, 0x005654c0: 65536.0}
WINDOWS = [(0x00444606, 6, 'launch: lateral exit offset before the copy into f+0x240')]

fails = 0
sha = hashlib.sha256(d).hexdigest()
print('X3AP.exe', len(d), 'B sha256', sha, 'MATCH' if sha == SHA else 'MISMATCH'); fails += sha != SHA
for va, exp, claim in BYTES:
    got = rd(va, len(bytes.fromhex(exp))).hex(' ')
    ok = got == exp; fails += not ok
    print(f'{"ok  " if ok else "FAIL"} {va:#010x} {got:44s} {claim}')
for cid, va in SA_CASES.items():  # SA dispatcher jump table [cmd*4 + 0x4690f0]
    got = struct.unpack('<I', rd(0x004690f0 + 4 * cid, 4))[0]; ok = got == va; fails += not ok
    print(f'{"ok  " if ok else "FAIL"} SA id {cid:#x} case {got:#010x}')
for o, va in ORDERS.items():  # order switch 0x453cf7: byte index at 0x459694, targets at 0x45963c
    got = struct.unpack('<I', rd(0x0045963c + 4 * rd(0x00459694 + o, 1)[0], 4))[0]; ok = got == va; fails += not ok
    print(f'{"ok  " if ok else "FAIL"} order {o:#x} handler {got:#010x}')
for va, val in DOUBLES.items():
    got = struct.unpack('<d', rd(va, 8))[0]; ok = abs(got - val) < 1e-12; fails += not ok
    print(f'{"ok  " if ok else "FAIL"} double {va:#010x} = {got!r}')
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
n_checks = len(BYTES) + len(SA_CASES) + len(ORDERS) + len(DOUBLES) + len(WINDOWS) + 1
print('RESULT', 'PASS' if fails == 0 else f'FAIL ({fails})', f'({n_checks} checks)')
