#!/usr/bin/env python3
"""Re-read every byte string quoted in docs/reverse-engineering/ship-movement.md
from X3AP.exe through the PE section map and print RESULT PASS / FAIL.

  python3 verification/results/ship-movement/check_sites.py [path/to/X3AP.exe]

Checks, all read from the file on disk (no game launch, no Wine):
  1. SHA-256 of the image (the hash every reverse-engineering note quotes);
  2. each quoted instruction byte string at its VA;
  3. the jump tables the note decodes (object class, ship command word, SA_ case table)
     and three constants (1/65536, 2*pi, 65536.0);
  4. the SA_ name strings at the module name table 0x0057a7f0;
  5. hook-site suitability of the entry candidates: the number of rel32 calls that
     reach each entry, and that no rel32 branch anywhere in .text, no rel8/rel32
     branch inside the decoded containing function, and no dword in .rdata/.data
     points into the bytes a 5- or 6-byte patch would overwrite.
Requires python3-capstone for step 5 (linear sweep of the containing functions).
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
    name = data[s:s + 8].rstrip(b'\0').decode()
    vsize, rva, rsize, raw = struct.unpack_from('<IIII', data, s + 8)
    SECTIONS.append((name, rva, max(vsize, rsize), raw, rsize))


def off(va):
    rva = va - BASE
    for name, srva, size, raw, rsize in SECTIONS:
        if srva <= rva < srva + size:
            return raw + rva - srva
    raise ValueError(hex(va))


def rd(va, n):
    return data[off(va):off(va) + n]


def u32(va):
    return struct.unpack_from('<I', data, off(va))[0]


def cstr(va):
    o = off(va)
    return data[o:data.index(b'\0', o)].decode('latin1')


fails = []


def check(ok, label):
    print(('ok   ' if ok else 'FAIL ') + label)
    if not ok:
        fails.append(label)


digest = hashlib.sha256(data).hexdigest()
check(digest == SHA256, f'sha256 {digest} size {len(data)}')

# (va, quoted bytes, meaning)
SITES = [
    # time step
    (0x00452adc, 'a1 34 6f 60 00 8b 80 14 07 00 00 3d e8 03 00 00', 'simulate: dt = [*0x606f34+0x714], clamp 1000 ms'),
    (0x004597dd, '2b b3 a8 00 00 00', 'integrator: per-object dt = time - [obj+0xa8]'),
    (0x00453232, '0f bf 43 48 83 f8 14 0f 87 9e 61 00 00 0f b6 88 00 96 45 00 ff 24 8d e0 95 45 00', 'simulate: class switch'),
    (0x00453ce0, '0f bf 86 58 02 00 00 83 f8 24', 'simulate: ship command word [flight+0x258]'),
    # integrator, rotation
    (0x00459926, 'f7 c1 00 00 08 00', 'integrator: autopilot flag 0x80000 test (hook candidate C)'),
    (0x004597a5, '8b 4b 40 f7 c1 00 00 00 08', 'integrator: ECX = [obj+0x40] (live to site C)'),
    (0x004597db, '8b f7 2b b3 a8 00 00 00', 'integrator: ESI = time - [obj+0xa8], EDI = time'),
    (0x0045992c, '0f 84 a9 03 00 00', 'integrator: je 0x00459cdb (manual-rate path) after site C'),
    (0x00459943, '69 c0 b8 0d 00 00 8b 94 08 d0 00 00 00', 'integrator: record +0xd0 (rotation acceleration %)'),
    (0x00459953, '8b 88 b0 02 00 00', 'integrator: flight+0x2b0 rotation-accel factor'),
    (0x00459969, 'b9 64 00 00 00 f7 ea f7 f9', 'integrator: x/100'),
    (0x00459978, 'c7 44 24 28 00 00 01 00', 'integrator: non-ship accel factor 1.0'),
    (0x004599d4, 'e8 47 13 fe ff', 'integrator: call 0x0043ad20 (roll axis)'),
    (0x004599aa, '3b c7 0f 84 ea 00 00 00', 'integrator: axis skipped when the angular acceleration is 0'),
    (0x0043ad20, '83 ec 08 55 56', 'per-axis controller entry (hook candidate D)'),
    (0x0043ad58, 'c2 1c 00', 'per-axis controller ret 0x1c'),
    # integrator, translation
    (0x0045a590, '8b 43 10 8b 94 0e 20 ff ff ff', 'velocity = basis row 2 x speed [obj+0x10]'),
    (0x0045a5aa, 'f7 ea 05 00 80 00 00 83 d2 00 0f ac d0 10', '16.16 multiply'),
    (0x0045a5bb, '89 04 11', 'store physics+0x100..0x108'),
    (0x0045a5d4, '66 83 b8 58 02 00 00 05', 'lateral slip skipped while docking (command 5)'),
    (0x0045a619, '03 84 24 90 00 00 00', 'slip += lateral part of the old velocity'),
    (0x0045a69a, '8b 4b 60 03 88 00 01 00 00', 'total velocity = [obj+0x60] + physics+0x100'),
    (0x0045a6e9, 'e8 32 7f fb ff', 'call 0x00412620: v*dt/1000'),
    (0x0045a70d, '8b 08 03 ca', 'position x += dx'),
    (0x0045a74e, 'b9 d0 07 00 00 2b 4c 24 24', 'slip decay (2000-dt)/2000'),
    # speed slew (tail of 0x00452ad0)
    (0x004594ef, 'e8 8c 74 ff ff 39 46 14', 'desired speed clamped to max 0x00450980'),
    (0x0045950d, '8b 81 78 02 00 00', 'flight+0x278 speed limit'),
    (0x00459562, 'e8 19 76 ff ff', 'call 0x00450b80 acceleration'),
    (0x0045956c, 'b8 d3 4d 62 10', '/1000 magic'),
    (0x0045958e, '85 c9 75 05 b9 01 00 00 00', 'minimum speed step 1'),
    (0x004595a3, '89 56 10', 'store current speed'),
    # caps
    (0x00450bc6, '69 d2 b8 0d 00 00 a1 d4 6f 60 00 8b 89 74 02 00 00 85 c9 8b 44 02 48', 'acceleration = ship record +0x48 x (1 + flight+0x274)'),
    (0x00450be3, '81 c1 00 00 01 00', 'acceleration: 1.0 + engine tuning'),
    (0x0043a6a6, '8b 15 d4 6f 60 00 0f bf c9 69 c9 b8 0d 00 00 8b 44 11 44', 'base max speed = ship record +0x44'),
    (0x00450992, 'e8 f9 9c fe ff', 'max speed 0x00450980 starts from 0x0043a690'),
    (0x004509e8, '8b 46 40 a9 00 00 00 40', 'max speed: hull ramp skipped when obj+0x40 & 0x40000000'),
    (0x00450a13, 'e8 f8 de ff ff', 'max speed: type maximum hull 0x0044e910'),
    (0x00450a20, 'c1 e1 04 2b cf 03 c9', 'max speed: y0 = 30 % of speed'),
    (0x00450a3a, '6b c9 5a', 'max speed: x1 = 90 % of max hull'),
    (0x00450a53, '8d 0c 9b 03 c9', 'max speed: x0 = 10 % of max hull'),
    (0x00450a6e, '8b 8e ec 00 00 00 e8 a7 f0 fd ff', 'max speed: ramp(current hull obj+0xec)'),
    # steering dispatcher
    (0x0044d020, '55 8b ec 83 e4 f0 83 ec 14', 'steering dispatcher prologue (hook candidate B)'),
    (0x0044d060, '83 f8 64', 'dead zone 100 units'),
    (0x0044d07f, '66 83 7b 48 07 75 0b 8b 7b 50 81 c7 70 08 00 00', 'ship basis flight+0x870'),
    (0x0044d0fc, '3d 3b ff 00 00', 'aligned test cos >= 0xff3b'),
    (0x0044d115, '0f bf 53 4a 0f bf 4b 48 69 d2 b8 0d 00 00 8b 04 8d b8 6f 60 00', 'rates from record +8/+0xc/+0x10'),
    (0x0044d112, 'c2 08 00', 'dispatcher ret 8'),
    (0x0044d12a, '8b 4c 10 08 03 c2 8b 50 0c 3b ca 8b 40 10 53 7e 15 3b d0 8b 55 0c 7f c5', 'dispatcher: yaw>pitch? pitch>roll -> 0x0044ccc0'),
    (0x0044d150, '3b c8 8b 55 0c 7f b0 e8 14 f8 ff ff', 'dispatcher: yaw>roll -> 0x0044ccc0 else 0x0044c970'),
    (0x0046065a, '81 fb 79 01 00 00', 'SA_ dispatcher: 0x17a cases'),
    (0x004344c7, 'c7 04 c8 30 06 46 00', 'SA_ module registration of 0x00460630'),
    # steering leaves
    (0x0044ccc0, '83 ec 18 8b 02', 'yaw/pitch leaf entry (hook candidate A)'),
    (0x0044cfe7, '81 fe 00 40 00 00 7f 08 81 fe 00 c0 ff ff 7d 02 33 c0', 'pitch suppressed while |yaw| > 90 deg'),
    (0x0044cffd, '89 77 30 89 47 34 c7 47 38 00 00 00 00', 'writes yaw, pitch, roll=0'),
    (0x0044d011, 'c2 04 00', 'leaf ret 4'),
    (0x0044c620, '83 ec 18 8b 02', 'roll+yaw leaf entry (hook candidate A)'),
    (0x0044c93f, '3d 00 20 00 00', 'yaw suppressed while |roll| > 45 deg'),
    (0x0044c94c, '89 48 30 c7 40 34 00 00 00 00 89 70 38', 'writes yaw, pitch=0, roll'),
    (0x0044c960, 'c2 04 00', 'leaf ret 4'),
    (0x0044c970, '83 ec 18 8b 02', 'roll+pitch leaf entry (hook candidate A)'),
    (0x0044cc91, '3d 00 20 00 00', 'pitch suppressed while |roll| > 45 deg'),
    (0x0044cc9e, 'c7 40 30 00 00 00 00 89 48 34 89 70 38', 'writes yaw=0, pitch, roll'),
    (0x0044ccb2, 'c2 04 00', 'leaf ret 4'),
    # commands
    (0x004564cd, '8b 0d 34 6f 60 00 8b 91 18 07 00 00 2b 96 10 02 00 00 0f 88 f8 2e 00 00', 'MovePos: think timer flight+0x210'),
    (0x0045651b, 'e8 40 a8 ff ff 85 c0 74 0a 53 8b d7 e8 94 67 ff ff', 'MovePos: yaw rate != 0 -> 0x0044ccc0'),
    (0x0045652e, '57 53 e8 eb 6a ff ff', 'MovePos: else 0x0044d020'),
    (0x0045654b, '68 88 13 00 00', 'MovePos: speed 5000 at distance 0'),
    (0x0045655c, 'e8 cf 34 01 00', 'MovePos: brake distance v*v/a'),
    (0x00456568, 'e8 b3 95 fd ff 89 43 14', 'MovePos: desired speed = ramp'),
    (0x00456576, '3b b0 60 02 00 00', 'MovePos: arrival distance <= flight+0x260'),
    (0x00456584, '89 4b 14', 'MovePos: desired speed 0 on arrival'),
    (0x004543da, '8b 8b a4 00 00 00 8d 84 09 a8 61 00 00', 'dock: final zone 2r + 25000'),
    (0x004543f3, '33 c0 39 84 24 c0 02 00 00 89 43 14 89 43 10 89 43 60 89 43 64 89 43 68', 'dock: final zone zeroes speeds and slip'),
    (0x004544f3, 'e8 f8 4b fc ff', 'dock: kinematic glide 0x004190f0'),
    (0x00454745, 'e8 76 85 ff ff', 'dock approach: 0x0044ccc0'),
    (0x00454770, '3d 1e 05 00 00', 'dock: roll match when errors < 0x51e'),
    (0x00454897, '3d 33 f3 00 00', 'dock: facing test cos < 0xf333'),
    (0x004548a5, 'c7 43 14 f4 01 00 00', 'dock: desired speed 500 while not facing'),
    (0x00454323, '8b 82 cc 00 00 00 03 8b a4 00 00 00 2d 00 00 01 00', 'dock/gate arrival radius |v|/10 + r scaled by time factor'),
    (0x00454b48, '3d b8 56 00 00', 'FlyBefore: approach distance floor 22200'),
    (0x00454b5d, '3d 98 b1 01 00', 'FlyBefore: approach distance cap 111000'),
    (0x00458290, '8b 48 1c 8b 50 18 6a 00 51 52', 'Escort: R(leader alpha, beta, 0)'),
    (0x004582ac, '81 c6 40 02 00 00', 'Escort: offset vector flight+0x240'),
    (0x004582c0, 'e8 db 8a 09 00', 'Escort: rotate offset'),
    (0x004582cf, '8b 48 30 8b 50 34', 'Escort: + leader position'),
    (0x0044049d, '8b 4c 07 50 6b c9 32 89 8a 0c 02 00 00', 'creation: think interval = record +0x50 x 50 ms'),
    (0x004ee1c9, '8b 44 24 08 8b 14 24 b9 00 80 00 00 f7 ea f7 f9', 'rand() x n / 0x8000'),
    # SA_ writers
    (0x00462352, '66 c7 82 58 02 00 00 19 00', 'SA_MovePos -> command 0x19'),
    (0x0046236d, '89 91 40 02 00 00', 'SA_MovePos: target flight+0x240'),
    (0x00462361, '89 91 60 02 00 00', 'SA_MovePos: radius flight+0x260'),
    (0x004618e3, '66 c7 82 58 02 00 00 14 00', 'SA_EscortObject -> command 0x14'),
    (0x004618ef, '89 81 24 02 00 00', 'SA_EscortObject: leader flight+0x224'),
    (0x004619ac, '66 c7 81 58 02 00 00 15 00', 'SA_FollowObject -> command 0x15'),
    (0x00460c72, '66 c7 81 58 02 00 00 05 00', 'SA_FlyObjectIntoContainer -> command 5'),
    (0x0046108c, '66 c7 82 58 02 00 00 12 00', 'SA_AvoidObjectCollision -> command 0x12'),
    (0x004641f5, '89 50 14', 'SA_SetDesiredSpeed -> obj+0x14'),
    (0x0046329f, '89 78 24 89 50 28 89 48 2c', 'SA_SetRotSpeed -> obj+0x24/0x28/0x2c'),
    (0x004642d9, '89 82 74 02 00 00', 'SA_SetExtraSpeed -> flight+0x274'),
    (0x0046433a, '89 82 70 02 00 00', 'SA_SetExtraRotSpeed -> flight+0x270'),
    (0x004676b2, '89 81 b0 02 00 00', 'SA_SetShipRotAccel -> flight+0x2b0'),
    (0x00465d3b, '89 91 78 02 00 00', 'SA_SetSpeedLimit -> flight+0x278'),
    (0x00465d6f, '89 91 7c 02 00 00', 'SA_SetMaxSpeedOverride -> flight+0x27c'),
    (0x00465da3, '89 91 80 02 00 00', 'SA_SetMaxStrafeSpeedOverride -> flight+0x280'),
    (0x00463e88, '81 48 40 00 00 08 00', 'SA_SetAutopilot sets 0x80000'),
    (0x00463ea7, '81 60 40 ff ff f7 ff', 'SA_SetAutopilot clears 0x80000'),
    # SA getters
    (0x00462120, '8b 50 18', 'SA_GetRotAlpha <- obj+0x18'),
    (0x00462147, '8b 40 1c', 'SA_GetRotBeta <- obj+0x1c'),
    (0x00462210, '8b 48 20', 'SA_GetRotGamma <- obj+0x20'),
    (0x00462909, '8b 48 10', 'SA_GetSpeed <- obj+0x10'),
    (0x00462931, 'e8 4a e0 fe ff', 'SA_GetMaxSpeed = 0x00450980'),
    (0x0046288b, '8b 50 70 8b 42 70', 'SA_GetSize <- physics+0x70'),
    # type loader (TShips)
    (0x004364d0, 'e8 eb 4a 0b 00 89 45 08', 'rotation X column -> record +0x08 (16.16)'),
    (0x004364da, 'e8 e1 4a 0b 00 89 45 0c', 'rotation Y column -> record +0x0c'),
    (0x004364e4, 'e8 d7 4a 0b 00', 'rotation Z column parse'),
    (0x004364f1, '89 45 10', 'rotation Z -> record +0x10'),
    (0x004373eb, 'e8 20 32 0b 00 89 45 44', 'TShips speed -> record +0x44'),
    (0x004373f5, 'e8 16 32 0b 00 89 45 48', 'TShips acceleration -> record +0x48'),
    (0x0043740e, '89 45 50', 'TShips reaction delay -> record +0x50'),
    (0x004376e9, '89 85 d0 00 00 00', 'TShips rotation acceleration -> record +0xd0'),
    (0x004376f1, 'c7 85 d0 00 00 00 64 00 00 00', 'record +0xd0 default 100 (old files)'),
    (0x004ec5ce, 'dc 0d c0 54 56 00', 'decimal parser x 65536.0'),
]
for va, hexs, what in SITES:
    want = bytes.fromhex(hexs.replace(' ', ''))
    got = rd(va, len(want))
    check(got == want, f'{va:08x} {hexs} {what}' + ('' if got == want else f' got {got.hex(" ")}'))

# jump tables
cls_idx = rd(0x00459600, 0x15)
cls_tab = [u32(0x004595e0 + 4 * i) for i in range(8)]
check(cls_tab[cls_idx[7]] == 0x00453cd7, 'class 7 (ship) -> 0x00453cd7')
check(cls_tab[cls_idx[10]] == 0x0045324d, 'class 10 (missile) -> 0x0045324d')
cmd_idx = rd(0x00459694, 0x25)
cmd_tab = [u32(0x0045963c + 4 * i) for i in range(max(cmd_idx) + 1)]
for cmd, tgt in [(0, 0x458f8e), (4, 0x4549c2), (5, 0x453fc2), (6, 0x4554af), (0x12, 0x456722),
                 (0x14, 0x45820d), (0x15, 0x458aea), (0x19, 0x4564cd), (0x1d, 0x453cfe), (0x23, 0x45659c)]:
    check(cmd_tab[cmd_idx[cmd]] == tgt, f'ship command {cmd:#x} -> {tgt:#010x}')
sa = [u32(0x004690f0 + 4 * i) for i in range(0x17a)]
for idx, tgt, name in [(5 + 4, 0x46082e, 'SA_FlyObjectIntoContainer'), (15, 0x46104c, 'SA_AvoidObjectCollision'),
                       (20, 0x4618a6, 'SA_EscortObject'), (27, 0x46196f, 'SA_FollowObject'),
                       (30, 0x46232c, 'SA_MovePos'), (66, 0x4641c6, 'SA_SetDesiredSpeed'),
                       (69, 0x463280, 'SA_SetRotSpeed'), (288, 0x463e6e, 'SA_SetAutopilot')]:
    nm = cstr(u32(0x0057a7f0 + 4 * idx))
    check(sa[idx] == tgt and nm == name, f'SA case {idx} {nm} -> {sa[idx]:#010x}')
check(abs(struct.unpack_from('<f', data, off(0x005654e0))[0] - 1 / 65536) < 1e-12, '0x005654e0 = 1/65536 (float)')
check(abs(struct.unpack_from('<f', data, off(0x005654f8))[0] - 6.2831853) < 1e-6, '0x005654f8 = 2*pi (float)')
check(struct.unpack_from('<d', data, off(0x005654c0))[0] == 65536.0, '0x005654c0 = 65536.0 (double)')

# hook-site suitability
text = next(s for s in SECTIONS if s[0] == '.text')
t_raw, t_size, t_rva = text[3], text[4], text[1]
tb = data[t_raw:t_raw + t_size]


def rel32_hits(target_lo, target_hi, opcodes=(0xe8, 0xe9)):
    hits = []
    for i in range(len(tb) - 5):
        b = tb[i]
        if b in opcodes:
            src = BASE + t_rva + i
            tgt = (src + 5 + struct.unpack_from('<i', tb, i + 1)[0]) & 0xffffffff
            if target_lo <= tgt < target_hi:
                hits.append((src, b, tgt))
        if b == 0x0f and 0x80 <= tb[i + 1] <= 0x8f and i + 6 <= len(tb):
            src = BASE + t_rva + i
            tgt = (src + 6 + struct.unpack_from('<i', tb, i + 2)[0]) & 0xffffffff
            if target_lo <= tgt < target_hi:
                hits.append((src, 0x0f, tgt))
    return hits


ENTRIES = [  # (entry, patch length, containing function range, expected rel32 call count)
    (0x0044ccc0, 5, (0x0044ccc0, 0x0044d014), 17),
    (0x0044c620, 5, (0x0044c620, 0x0044c962), 2),
    (0x0044c970, 5, (0x0044c970, 0x0044ccb3), 2),
    (0x0044d020, 6, (0x0044d020, 0x0044d163), 15),
    (0x0043ad20, 5, (0x0043ad20, 0x0043b0a7), 20),
    (0x00459926, 6, (0x004596e0, 0x0045b627), None),
]
import capstone  # noqa: E402
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
rdata = [s for s in SECTIONS if s[0] in ('.rdata', '.data')]
for entry, n, (flo, fhi), ncalls in ENTRIES:
    calls = [h for h in rel32_hits(entry, entry + 1) if h[1] == 0xe8]
    if ncalls is not None:
        check(len(calls) == ncalls, f'{entry:08x} reached by {len(calls)} rel32 calls (expected {ncalls})')
    interior = rel32_hits(entry + 1, entry + n)
    check(not interior, f'{entry:08x} no rel32 branch into +1..+{n - 1} (found {len(interior)})')
    bad = []
    boundary = False
    for ins in md.disasm(rd(flo, fhi - flo), flo):
        if ins.address == entry + n:
            boundary = True
        if ins.mnemonic.startswith('j') or ins.mnemonic == 'call':
            try:
                t = int(ins.op_str, 16)
            except ValueError:
                continue
            if entry < t < entry + n:
                bad.append(ins.address)
    check(boundary, f'{entry:08x} +{n} is an instruction boundary in the linear sweep')
    check(not bad, f'{entry:08x} no branch in {flo:08x}..{fhi:08x} into the patch bytes (found {len(bad)})')
    drefs = 0
    for name, srva, size, raw, rsize in rdata:
        blob = data[raw:raw + rsize]
        for i in range(0, len(blob) - 3, 4):
            v = struct.unpack_from('<I', blob, i)[0]
            if entry <= v < entry + n:
                drefs += 1
    check(drefs == 0, f'{entry:08x} no aligned data dword points into the patch bytes (found {drefs})')

print(f'checks {len(SITES)} byte strings; failures {len(fails)}')
print('RESULT ' + ('PASS' if not fails else 'FAIL'))
sys.exit(0 if not fails else 1)
