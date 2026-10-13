#!/usr/bin/env python3
"""Re-read the EXE facts of docs/reverse-engineering/ship-navigation-ai.md section 10.11 (gate traffic controller,
stage B): the order-5 misalignment test and the flag 0x10000000, the readers of f+0x230/f+0x240, site B against the
slew loop, and the order-0 path with the integrator's use of the remainders obj+0x30/+0x34/+0x38.

Byte strings are compared exactly; three sweeps run over a linear capstone pass of .text:
  * every immediate 0x10000000 operand: the setters on [reg+0x40] and the readers of a register loaded from
    [reg+0x40] (or a memory test on [reg+0x40]) within the previous 8 instructions;
  * every and-mask on [reg+0x40] that clears bit 28;
  * every f+0x230 reference (displacement, lea or add of 0x230; stack slots excluded) in 0x00440000..0x0046a000, attributed to the order
    handler or SA case whose entry is the nearest below it (attribution by address, [i]).
Prints RESULT PASS only if every byte string, table entry and sweep expectation holds.

    python3 -I verification/results/ship-navigation-ai/gates/stageb_sites.py [path/to/X3AP.exe] > stageb_sites_out.txt
"""
import hashlib, re, struct, sys
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
    # Q1: the misalignment test of order 5 and what it measures
    (0x0045413e, '8d b7 40 02 00 00', 'order 5: esi = &f+0x240 (the stored point)'),
    (0x0045418b, '8b c6', 'order 5: eax = &f+0x240 ...'),
    (0x004541a6, '89 84 24 cc 02 00 00', '... [esp+0x2cc] = &f+0x240 (a pointer to the live field, not the copy [esp+0x4a0])'),
    (0x004541b4, '66 83 78 48 12', 'order 5: target class 0x12 (gate) ...'),
    (0x004541c7, '0f 84 94 00 00 00', '... -> 0x454261: skips the f+0x230 rendezvous read and the pointer switch'),
    (0x004541cd, '81 c7 30 02 00 00', 'order 5, non-gate only: edi = &f+0x230 (rendezvous)'),
    (0x004541fa, '89 bc 24 cc 02 00 00', 'order 5, non-gate only: [esp+0x2cc] = &f+0x230 while far'),
    (0x00454733, '8d 94 24 a4 04 00 00', 'order 5: leaf argument edx = &[esp+0x4a0] (the copy; site A redirects it)'),
    (0x00454745, 'e8 76 85 ff ff', 'order 5: leaf 0x44ccc0 (site A) runs before the misalignment test'),
    (0x00454760, '66 83 78 48 12 74 72', 'order 5: gate target skips the port-roll match -> 0x4547d9'),
    (0x004547d9, '8b 94 24 cc 02 00 00', 'misaligned test: edx = [esp+0x2cc] = &f+0x240 (gate)'),
    (0x004547e0, '8b 4b 70 83 c1 30', 'misaligned test: ecx = &phys+0x30 (ship centre)'),
    (0x004547ee, 'e8 9d dc fb ff', 'misaligned test: 0x412490 -> [edx] - [ecx] = f+0x240 - centre'),
    (0x00454821, 'e8 8a 80 04 00', 'misaligned test: 0x49c8b0 normalises to 16.16 unit length'),
    (0x0049c8df, 'dc 0d 10 55 56 00', '0x49c8b0: x [0x565510] (1/65536)'),
    (0x00454826, '8b c3 e8 f3 bc ff ff', 'misaligned test: basis 0x450520(ship)'),
    (0x00450520, '66 83 78 48 07 75 09 8b 40 50 05 70 08 00 00 c3', '0x450520: class 7 -> f+0x870 (flight basis)'),
    (0x0045482f, '8b 41 20', 'misaligned test: basis +0x20 (row 2 x, the nose)'),
    (0x0045484f, '8b 51 24', 'misaligned test: row 2 y'),
    (0x00454873, '8b 41 28', 'misaligned test: row 2 z'),
    (0x00454897, '3d 33 f3 00 00 7d 1a', 'misaligned test: nose . dir < 0xf333 (0.95, 18.195 deg)'),
    (0x004548a5, 'c7 43 14 f4 01 00 00', 'misaligned: desired [obj+0x14] = 500 (1 m/s)'),
    (0x004548ac, '81 48 40 00 00 00 10', 'misaligned: [target+0x40] |= 0x10000000'),
    (0x004548b3, 'e9 25 4b 00 00', 'misaligned: jmp 0x4593dd (site B), skipping the aligned branch'),
    (0x004548cf, '81 48 40 00 00 00 02', 'aligned, gate: [gate+0x40] |= 0x2000000'),
    (0x0045498f, '81 48 40 00 00 00 10', 'aligned (ramp tail): [target+0x40] |= 0x10000000'),
    (0x00454996, 'e9 42 4a 00 00', 'aligned: jmp 0x4593dd (site B)'),
    (0x004543ad, '81 48 40 00 00 00 10', 'order 5 port branch: [target+0x40] |= 0x10000000'),
    (0x00455083, '81 4a 40 00 00 00 10', 'order 4: [target+0x40] |= 0x10000000'),
    (0x00455196, '81 4f 40 00 00 00 10', 'order 0x1b (player landing): [target+0x40] |= 0x10000000'),
    # site B and the slew loop that follows the object loop
    (0x004593dd, '8b 1b 83 3b 00 0f 85 78 97 ff ff', 'site B window: object-loop continue (mov ebx,[ebx] | cmp [ebx],0), jne 0x452b60'),
    (0x004594b0, '0f b7 46 48 66 3d 07 00 74 10 66 3d 13 00 74 0a 66 3d 0a 00', 'slew loop: classes 7, 0x13, 0xa only (a gate is skipped)'),
    (0x004594ef, 'e8 8c 74 ff ff 39 46 14 7e 0a', 'slew: desired > vmax ...'),
    (0x00459500, '89 46 14', '... desired = vmax'),
    (0x0045950d, '8b 81 78 02 00 00', 'slew, class 7: speed limit f+0x278 ...'),
    (0x0045951c, '89 46 14', '... desired = min(desired, limit)'),
    (0x0045951f, '8b 46 40 8b d0 c1 ea 14 f6 c2 01 74 2f a9 00 00 08 00 74 28 a9 00 00 00 10',
     'slew, class 7: flags 0x100000 and 0x80000, then 0x10000000 (or now < f+0x180) ...'),
    (0x0045954d, '33 c0 89 46 14 89 46 30 89 46 34 89 46 38', '... desired and remainders = 0 (the only reader of 0x10000000)'),
    (0x0045b741, 'bd ff ff ff ed', 'post pass: ebp = 0xedffffff ...'),
    (0x0045b7f9, '21 6e 40', '... [obj+0x40] &= ebp: clears 0x10000000 and 0x2000000 on every object each frame'),
    (0x0044b124, 'f7 46 40 00 00 00 02', 'gate post-pass update 0x44b0c0: reads 0x2000000 (not 0x10000000)'),
    # Q2: f+0x230 / f+0x240 writers and readers on a gate leg
    (0x0045406a, '66 83 7f 48 07 0f 85 c3 00 00 00', 'order 5: per-frame rewrite of f+0x240/f+0x230 only for a class-7 target'),
    (0x004540b8, '05 40 02 00 00', 'order 5, class-7 target: f+0x240 = port position'),
    (0x0045411e, '81 c1 30 02 00 00', 'order 5, class-7 target: f+0x230 = rendezvous'),
    (0x00460c25, '89 91 40 02 00 00', 'SA_FlyObjectIntoContainer, gate: f+0x240 = B (or the origin for a capital)'),
    (0x00460c49, '8b 91 40 02 00 00 8d 81 40 02 00 00 81 c1 30 02 00 00', 'SA_FlyObjectIntoContainer, gate: f+0x230 = copy of f+0x240'),
    (0x00460c72, '66 c7 81 58 02 00 00 05 00', 'SA_FlyObjectIntoContainer: order 5'),
    (0x0044e492, '66 83 f9 1e 75 7e', 'proximity handler 0x44e450: order 0x1e only ...'),
    (0x0044e4af, '8d b0 30 02 00 00', '... reads f+0x230 (the 0x1e start point)'),
    (0x0044e4bd, '3d e0 5a 01 00', '... within 88800 units (177.6 m): no warn'),
    (0x00456f32, '8d 86 30 02 00 00', 'order 0x12: reads its own f+0x230'),
    (0x00457842, '89 91 30 02 00 00', 'order 0x12: f+0x230 = ship position'),
    (0x004610df, '81 c1 30 02 00 00', 'SA_AvoidObjectCollision: f+0x230 = ship position'),
    (0x00461211, '81 c2 30 02 00 00', 'SA_ClearObjectFromHangar (0x1e): f+0x230 = ship position'),
    (0x0044997e, '66 83 be 58 02 00 00 05', 'SA_LandAllObjectsInHangar core 0x449930: ship on order 5 ...'),
    (0x0044998c, '39 ae 24 02 00 00', '... to this container ...'),
    (0x0044999b, '8b 96 40 02 00 00', '... reads f+0x240 ...'),
    (0x00449a05, '66 c7 86 58 02 00 00 00 00', '... within r/2 of it: order 0'),
    (0x004624f9, 'e8 32 74 fe ff', 'SA case 0x121 SA_LandAllObjectsInHangar -> 0x449930'),
    (0x00478bc7, '8b 86 30 02 00 00', 'serializer 0x478690: copies +0x218..+0x24c (save [i])'),
    (0x0047a2c6, '89 8b 30 02 00 00', 'deserializer 0x479d10: restores +0x218..+0x24c (load [i])'),
    # Q4: order 0, order 0x1f's end and the integrator
    (0x00458f8e, 'f7 43 40 00 00 08 00 74 16 33 ff f6 43 44 04 75 03 89 7b 14 89 be 24 02 00 00 e9 33 01 00 00',
     'order 0, autopilot: desired = 0 (unless [obj+0x44] & 4), f+0x224 = 0, -> tail 0x4590e0'),
    (0x004590e0, '8b 53 50 39 ba 18 02 00 00', 'order 0 tail: f+0x218 test (no remainder write)'),
    (0x00459926, 'f7 c1 00 00 08 00 0f 84 a9 03 00 00', 'integrator: autopilot 0x80000 -> controller branch'),
    (0x00459980, '8b 43 38 33 ff 3b c7 7e 7a', 'integrator: roll remainder e = [obj+0x38] ...'),
    (0x004599d4, 'e8 47 13 fe ff', '... controller 0x43ad20 ...'),
    (0x004599f4, '89 43 38', '... e -= delta (roll)'),
    (0x00459b0e, '89 43 34', 'integrator: e -= delta (pitch)'),
    (0x00459c2c, '89 43 30', 'integrator: e -= delta (yaw)'),
    (0x004592d9, '3d 70 fd 00 00 0f 8c 7c 00 00 00', 'order 0x1f: nose within 8.11 deg ...'),
    (0x004592e4, '8b 87 48 02 00 00 3d 00 00 00 80 75 1d', '... no roll target ...'),
    (0x004592f4, '33 c0 89 43 38 6a 01', '... roll remainder = 0 only; yaw/pitch remainders stay; order 0, result 1'),
    (0x0045932b, '33 c9 3b c1 89 4b 30 89 4b 34 89 43 38', 'order 0x1f with roll target: yaw = pitch = 0'),
    (0x004593a6, 'e8 15 39 ff ff', 'order 0x1f: leaf only on the not-aligned path'),
    (0x00454b70, '3b f0 7f 2d', 'order 4: arrival test (fails -> 0x454ba1, toward the dispatcher)'),
    (0x00454b91, '89 43 14 89 43 10 e8 c4 b9 00 00', 'order 4 arrival (P2): speeds only, wake; no remainder write'),
    (0x00454f83, 'e8 98 80 ff ff', 'order 4: dispatcher 0x44d020 after the arrival exit (not reached on the P2 frame)'),
]
SA_CASES = {0x09: 0x0046082e, 0x0d: 0x0046117e, 0x0f: 0x0046104c, 0x121: 0x004624e4}
ORDERS = {0x00: 0x00458f8e, 0x04: 0x004549c2, 0x1b: 0x0045512c, 0x05: 0x00453fc2, 0x12: 0x00456722, 0x1e: 0x00457bc3, 0x1f: 0x0045920e}
DOUBLES = {0x00565510: 1 / 65536}
EXPECT_SETTERS = {0x004543ad, 0x004548ac, 0x0045498f, 0x00455083, 0x00455196}
EXPECT_READERS = {0x00459533}

fails = 0
sha = hashlib.sha256(d).hexdigest()
print('X3AP.exe', len(d), 'B sha256', sha, 'MATCH' if sha == SHA else 'MISMATCH'); fails += sha != SHA
for va, exp, claim in BYTES:
    got = rd(va, len(bytes.fromhex(exp))).hex(' ')
    ok = got == exp; fails += not ok
    print(f'{"ok  " if ok else "FAIL"} {va:#010x} {got[:44]:44s} {claim}')
for cid, va in SA_CASES.items():  # SA dispatcher jump table [cmd*4 + 0x4690f0]
    got = struct.unpack('<I', rd(0x004690f0 + 4 * cid, 4))[0]; ok = got == va; fails += not ok
    print(f'{"ok  " if ok else "FAIL"} SA id {cid:#x} case {got:#010x}')
order_tab = {}
for o in range(0x26):  # order switch: byte index at 0x459694, targets at 0x45963c
    order_tab[o] = struct.unpack('<I', rd(0x0045963c + 4 * rd(0x00459694 + o, 1)[0], 4))[0]
for o, va in ORDERS.items():
    ok = order_tab[o] == va; fails += not ok
    print(f'{"ok  " if ok else "FAIL"} order {o:#x} handler {order_tab[o]:#010x}')
for va, val in DOUBLES.items():
    got = struct.unpack('<d', rd(va, 8))[0]; ok = abs(got - val) < 1e-18; fails += not ok
    print(f'{"ok  " if ok else "FAIL"} double {va:#010x} = {got!r}')

md = Cs(CS_ARCH_X86, CS_MODE_32); md.skipdata = True
ins = []
for n_, vs, sva, rs, ra in secs:
    if n_ == '.text':
        ins = [(i.address, i.mnemonic, i.op_str) for i in md.disasm(d[ra:ra + rs], base + sva)]
print('linear sweep of .text:', len(ins), 'instructions')

# Sweep 1: flag 0x10000000 on [reg+0x40].
setters, readers = set(), set()
for k, (a, m, o) in enumerate(ins):
    if not o.endswith('0x10000000'): continue
    if m == 'or' and re.match(r'dword ptr \[\w+ \+ 0x40\], 0x10000000$', o): setters.add(a)
    if m in ('test', 'and', 'bt'):
        if '+ 0x40]' in o: readers.add(a); continue
        reg = o.split(',')[0].strip()
        if re.match(r'^e[a-d]x$|^e[sd]i$|^ebp$', reg) and any(
                pm == 'mov' and po.startswith(reg + ', dword ptr [') and po.endswith('+ 0x40]')
                for _pa, pm, po in ins[max(0, k - 8):k]):
            readers.add(a)
ok = setters == EXPECT_SETTERS; fails += not ok
print(f'{"ok  " if ok else "FAIL"} sweep: or [reg+0x40], 0x10000000 at', ', '.join(hex(a) for a in sorted(setters)))
ok = readers == EXPECT_READERS; fails += not ok
print(f'{"ok  " if ok else "FAIL"} sweep: readers of bit 0x10000000 of [reg+0x40]:', ', '.join(hex(a) for a in sorted(readers)),
      '(the slew loop, class 7 only)')
clears = [a for a, m, o in ins if m == 'and' and re.match(r'dword ptr \[\w+ \+ 0x40\], ', o)
          and (not re.search(r', 0x[0-9a-f]+$', o) or not int(o.split(', ')[-1], 16) & 0x10000000)]
imm_clears = [a for a in clears if re.search(r', 0x[0-9a-f]+$', dict((x[0], x[2]) for x in ins)[a])]
reg_clears = [a for a in clears if a not in imm_clears]
ok = imm_clears == [] and reg_clears == [0x0045b7f9]; fails += not ok
print(f'{"ok  " if ok else "FAIL"} sweep: and-masks on [reg+0x40] clearing bit 28: immediate',
      [hex(a) for a in imm_clears], 'register', [hex(a) for a in reg_clears], '(ebp = 0xedffffff)')

# Sweep 2: f+0x230 references in the object/flight code, attributed by address.
sa_tab = {}
names_tab = 0x0057a7f0
for i in range(0x17a):
    tgt = struct.unpack('<I', rd(0x004690f0 + 4 * i, 4))[0]
    nm = struct.unpack('<I', rd(names_tab + 4 * i, 4))[0]
    o = off(nm); sa_tab[tgt] = d[o:d.index(b'\0', o)].decode('latin1')
starts = sorted([(va, 'order %#x' % o) for o, va in order_tab.items() if 0x00452ad0 <= va < 0x004596e0] +
                [(va, nm) for va, nm in sa_tab.items()] +
                [(0x0044e450, 'proximity 0x44e450'), (0x00449930, 'LandAll core 0x449930'), (0x004596e0, 'integrator')])
refs = [(a, m, o) for a, m, o in ins if 0x00440000 <= a < 0x0046a000 and 'esp' not in o and
        (re.search(r'\+ 0x230\]', o) or (m in ('add', 'lea') and re.search(r'(, 0x230$|\+ 0x230\]$)', o)))]
print('sweep: f+0x230 references in 0x440000..0x46a000:', len(refs))
for a, m, o in refs:
    owner = max((s for s in starts if s[0] <= a), default=(0, '?'))
    print(f'     {a:#010x} {m} {o:36s} in {owner[1]} ({owner[0]:#x}) [i] by address')
in5 = [a for a, m, o in refs if 0x00453fc2 <= a < 0x004549c2]
ok = in5 == [0x0045411e, 0x004541cd]; fails += not ok
print(f'{"ok  " if ok else "FAIL"} order 5 references f+0x230 only at', [hex(a) for a in in5], '(class-7 write, non-gate read)')

# Sweep 3: order 0 writes no remainder; the integrator's rotation part reads no order word.
o0 = [(a, m, o) for a, m, o in ins if 0x00458f8e <= a < 0x00459141 and m == 'mov' and re.match(r'dword ptr \[ebx \+ 0x3[048]\]', o)]
ok = o0 == []; fails += not ok
print(f'{"ok  " if ok else "FAIL"} order 0 (0x458f8e..0x459141) writes to [obj+0x30/0x34/0x38]:', len(o0))
rot = [(a, o) for a, m, o in ins if 0x00459926 <= a < 0x00459cdb and '0x258]' in o]
ok = rot == []; fails += not ok
print(f'{"ok  " if ok else "FAIL"} integrator rotation (0x459926..0x459cdb) reads of the order word f+0x258:', len(rot))
w14 = [a for a, m, o in ins if 0x004596e0 <= a < 0x0045a7a0 and m == 'mov' and re.match(r'dword ptr \[e[a-d]x \+ 0x14\]|dword ptr \[e[sd]i \+ 0x14\]', o)]
ok = w14 == []; fails += not ok
print(f'{"ok  " if ok else "FAIL"} integrator (0x4596e0..0x45a7a0) writes to [obj+0x14]:', len(w14))
jb = [a for a, m, o in ins if 0x00453fc2 <= a < 0x004549c2 and m == 'jmp' and o == '0x4593dd']
print(f'     order 5 exits to site B (jmp 0x4593dd): {len(jb)} at', ', '.join(hex(a) for a in jb))
n_checks = len(BYTES) + len(SA_CASES) + len(ORDERS) + len(DOUBLES) + 1 + 7
print('RESULT', 'PASS' if fails == 0 else f'FAIL ({fails})', f'({n_checks} checks)')
