#!/usr/bin/env python3
"""Engine facts behind the convoy lane's warn suppressions and the fixture's engine model
(docs/reverse-engineering/ship-navigation-ai.md section 10.14).

1. EXE: every instruction quoted in 10.14 is re-decoded with capstone through the PE section map of the installed
   X3AP.exe and compared with the expected text; the class tables of 0x0043fd50, 0x0043fd00, 0x0043fe80 and the
   obstacle filter 0x0044d2f0 are decoded; a sweep shows that SA_ClearObjectFromHangar writes no speed.
2. KC (Mayhem 4 addon/07.cat L/x3story.obj, the installed version): CollisionWarn's flag member, every writer of it in
   class 2004, the escort loop's avoid call, JumpToSector's arrival branches, __LeaveHyperspaceWarpIn's tail.
3. MSCI (installed loose scripts): SHA-256 and the deciding lines of the squadron leader hand-over.

    python3 -I verification/results/ship-navigation-ai/lane/lane_engine_sites.py [X3 dir] > lane_engine_sites_out.txt

Prints RESULT PASS only if every check holds. Prints no game bytes beyond instruction text and short script lines.
"""
import hashlib, html, re, struct, sys
from pathlib import Path

from capstone import Cs, CS_ARCH_X86, CS_MODE_32

X3 = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3')
SHA = 'fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab'
d = (X3 / 'X3AP.exe').read_bytes()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec, opt = struct.unpack_from('<H', d, pe + 6)[0], struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
secs = [struct.unpack_from('<IIII', d, pe + 24 + opt + 40 * i + 8) for i in range(nsec)]
md = Cs(CS_ARCH_X86, CS_MODE_32)
fails = 0
checks = 0


def off(va):
    r = va - base
    for vs, sva, rs, ra in secs:
        if sva <= r < sva + max(vs, rs):
            return ra + r - sva
    raise ValueError(hex(va))


def rd(va, n): o = off(va); return d[o:o + n]
def u32(va): return struct.unpack('<I', rd(va, 4))[0]


def insn(va):
    i = next(md.disasm(rd(va, 16), va))
    return ('%s %s' % (i.mnemonic, i.op_str)).strip()


def check(ok, text):
    global fails, checks
    checks += 1
    if not ok:
        fails += 1
    print('%s %s' % ('ok  ' if ok else 'FAIL', text))


INSNS = [  # (VA, expected instruction text, claim)
    # Q1: the part-box test 0x0044d9c0 (thiscall ECX = A; stack: part, reach, out, scale 16.16, flag; ret 0x14)
    (0x0044d9cf, 'mov edi, dword ptr [esi + 0xc]', 'box: child list of the part (recursion first)'),
    (0x0044d9f3, 'call 0x44d9c0', 'box: recursive call per child'),
    (0x0044da06, 'mov eax, dword ptr [esi + 0x140]', 'box: model id of the part'),
    (0x0044da15, 'call 0x4863c0', 'box: model lookup'),
    (0x0044da25, 'mov ecx, dword ptr [eax + 0xc]', 'box: model record list'),
    (0x0044da28, 'mov eax, dword ptr [ecx]', 'box: first record only'),
    (0x0044da2a, 'mov edx, dword ptr [eax + 0x20]', 'box: extent x (+0x24 y, +0x28 z)'),
    (0x0044da51, 'mov edx, dword ptr [esp + 0xc]', 'box: part scale [part+0x70] (16.16)'),
    (0x0044da63, 'mov edx, dword ptr [ebx + 0xa4]', 'box: + r_A per axis'),
    (0x0044da69, 'add edx, eax', 'box: e_x*s + r_A'),
    (0x0044da73, 'mov edx, dword ptr [ebp + 0x14]', 'box: * scale argument (1.2 = 0x13333 from every caller)'),
    (0x0044db0f, 'mov edx, dword ptr [eax + 0xb0]', 'box: A world position [A.body+0xb0]'),
    (0x0044db15, 'sub edx, dword ptr [esi + 0xb0]', 'box: minus the part world origin [part+0xb0]'),
    (0x0044db41, 'lea ecx, [esi + 0xc0]', 'box: part rotation [part+0xc0]'),
    (0x0044db4d, 'call 0x4f0c00', 'box: world -> part frame'),
    (0x0044db5e, 'cmp ecx, dword ptr [esp + 0x10]', 'box: inside test |c_x| < H_x (strict)'),
    (0x0044db9e, 'cmp dword ptr [ebp + 0x18], 0', 'box: flag != 0 -> inside test only'),
    (0x0044dc1a, 'fild dword ptr [ebp + 0xc]', 'box: reach'),
    (0x0044dc1d, 'fmul st(0), st(0)', 'box: reach^2'),
    (0x0044dc1f, 'fcompp', 'box: reach^2 vs squared distance to the inflated box'),
    (0x0044dc26, 'jnp 0x44dcf4', 'box: miss when reach^2 < dist^2'),
    (0x0044dc36, 'add ebx, 0x870', 'box: forward row of A flight+0x870'),
    (0x0044dc44, 'mov edx, dword ptr [ebx + 0x20]', 'box: row 2 x (nose)'),
    (0x0044dc4f, 'mov edx, dword ptr [ebp + 0xc]', 'box: D = nose * reach'),
    (0x0044dcb3, 'call 0x4f0c00', 'box: D into the part frame'),
    (0x0044dcd5, 'cmp eax, 1', 'slab: |D_i| < 1 (D_i == 0)'),
    (0x0044dce4, 'cmp eax, dword ptr [esp + ecx + 0x10]', 'slab: D_i == 0 -> miss iff |c_i| > H_i'),
    (0x0044dd09, 'sub eax, dword ptr [esp + ecx + 0x10]', 'slab: |c_i| - H_i'),
    (0x0044dd0e, 'idiv esi', 'slab: integer quotient q = (|c_i| - H_i) / D_i'),
    (0x0044dd1a, 'fchs', 'slab: interval [-|q|, +|q|] (symmetric: the sign of D_i is lost)'),
    (0x0044dd4f, 'fcom st(1)', 'slab: t_max vs t_min'),
    (0x0044dd5a, 'fcom st(2)', 'slab: t_max vs 0'),
    (0x0044dd7d, 'lea eax, [edi + 1]', 'box: hit -> out = part, return 1'),
    (0x00456c79, 'push 1', 'order 0x12 re-score: flag 1 (inside test only) ...'),
    (0x00456c80, 'push 0x13333', '... scale 1.2 ...'),
    (0x00456c9a, 'call 0x44d9c0', '... the third outside caller of the box test'),
    # Q1: the warn test 0x0044dd90 (stdcall A, B, d, &out; ret 0x10)
    (0x0044dda2, 'cmp dword ptr [edi + 0x10], 0', 'warn: A speed 0 ...'),
    (0x0044dda8, 'cmp dword ptr [edi + 0x14], 0', 'warn: ... and desired speed 0 -> false'),
    (0x0044ddb4, 'call 0x450b80', 'warn: a = acceleration of A (0 -> false)'),
    (0x0044ddd7, 'call 0x43fd50', 'warn: mesh B if capital (EAX = B), class 5/6, class 0x14 subtypes'),
    (0x0044de87, 'cmp eax, ecx', 'warn: 0.75 r_A <= r_B'),
    (0x0044dea1, 'mov eax, ebx', 'warn: vmax of B (EAX = B) ...'),
    (0x0044dea3, 'call 0x450980', '... B cannot move -> close-range rule'),
    (0x0044ded9, 'mov edx, 0x13333', 'warn: close range 1.2 (r_A + r_B)'),
    (0x0044def1, 'cmp dword ptr [esp + 0x18], 0', 'warn: close range true only for non-mesh B'),
    (0x0044dfa4, 'cmp edx, 0x64', 'warn: centres within 100 units -> u = nose'),
    (0x0044dfe1, 'call 0x49c8b0', 'warn: u = normalised A->B'),
    (0x0044e04c, 'mov dword ptr [esp + 0x14], eax', 'warn: cos = u . nose (16.16)'),
    (0x0044e05c, 'cmp dword ptr [ebx + 0x60], 0', 'warn: B behind, B speed 0, B velocity 0 -> false'),
    (0x0044e07c, 'cmp eax, 0xcccc', 'warn: B term when cos > 0.8 ...'),
    (0x0044e0a8, 'cmp eax, 0xffff3334', '... or (not attacking, B class 7, no B+0x44 bit 0x10) cos < -0.8'),
    (0x0044e12e, 'mov eax, dword ptr [ebx + 0x10]', 'warn: B term = -|v_B| (u . nose_B)'),
    (0x0044e159, 'call 0x43fd00', 'warn: B "large" (EAX = B) ...'),
    (0x0044e166, 'add esi, esi', '... and approaching -> B term doubled'),
    (0x0044e197, 'add dword ptr [esp + 0x20], eax', 'warn: L += r_A cos (cos > 0)'),
    (0x0044e1c2, 'add dword ptr [esp + 0xc], eax', 'warn: v_close = B term + |v_A| cos'),
    (0x0044e1d4, 'idiv ecx', 'warn: v_close^2 / a'),
    (0x0044e1d9, 'sar eax, 1', 'warn: b = v_close^2 / 2a'),
    (0x0044e1df, 'mov eax, 0x493e0', 'warn: b capped at 300000'),
    (0x0044e1f2, 'cmp edi, 0x14', 'warn: A on 0x14 vs its leader or a co-escort -> b/2'),
    (0x0044e23c, 'cmp edi, 0x15', 'warn: A on 0x15 vs its target or its own follower -> b/2'),
    (0x0044e282, 'mov ecx, dword ptr [ebp + 0x10]', 'warn: d ...'),
    (0x0044e289, 'cmp ecx, eax', '... <= r_A + r_B ...'),
    (0x0044e28d, 'cmp dword ptr [esp + 0x18], 0', '... and mesh B -> box test only'),
    (0x0044e2a6, 'push 0x13333', 'warn: box scale 1.2'),
    (0x0044e2ac, 'add eax, edx', 'warn: reach = |v_A| + b'),
    (0x0044e2b5, 'call 0x44d9c0', 'warn: close-range box test'),
    (0x0044e2d5, 'cmp ecx, edx', 'warn: range gate d <= b + L + r_A + r_B'),
    (0x0044e384, 'cmp eax, edx', 'warn: d <= b -> no cone'),
    (0x0044e3ad, 'call 0x44d9c0', 'warn: box test after the gate (and the cone when d > b)'),
    (0x0044e3cc, 'add ecx, 0x4e20', 'cone: r_A + r_B + 20000'),
    (0x0044e3e8, 'fmul dword ptr [0x5654e0]', 'cone: cos * 2^-16'),
    (0x0044e3f7, 'jp 0x44e436', 'cone: cos < 0 -> false'),
    (0x0044e40a, 'jne 0x44e43a', 'cone: cos^2 <= 1 - k^2 -> false'),
    # Q2: the pair entry 0x0044e450 and the obstacle filter 0x0044d2f0 (EDI = A, [esp+4] = B)
    (0x0044e455, 'test byte ptr [ebx + 0x44], 0x10', 'pair: A flag skip'),
    (0x0044e471, 'call 0x44d2f0', 'pair: obstacle filter'),
    (0x0044e4b8, 'call 0x419190', 'pair: 0x1e at a gate: Chebyshev |A - f+0x230| ...'),
    (0x0044e4bd, 'cmp eax, 0x15ae0', '... <= 88800 -> skip every obstacle'),
    (0x0044d2f1, 'cmp word ptr [edi + 0x48], 7', 'filter: A must be class 7'),
    (0x0044d31f, 'jmp dword ptr [eax*4 + 0x44d48c]', 'filter: B class switch 3..0x1f'),
    (0x0044d336, 'mov eax, dword ptr [ecx + edx + 0x14]', 'filter: A ship code'),
    (0x0044d355, 'jne 0x44d385', 'filter: A not capital -> no class/size skip'),
    (0x0044d35f, 'call 0x43fd00', 'filter: capital A: B class 7 not large -> ignored'),
    (0x0044d36c, 'cmp bx, 0xa', 'filter: capital A: B class 10 -> ignored'),
    (0x0044d378, 'call 0x43fe80', 'filter: capital A: expendable B -> ignored'),
    (0x0044d39f, 'cmp dword ptr [eax + 0x224], edi', 'filter: B escorting/following A -> ignored'),
    (0x0044d3b6, 'cmp si, 0x18', 'filter: A on 0x18 ignores expendable B'),
    (0x0044d3cb, 'cmp si, 0x24', 'filter: A on 0x24 ignores expendable B'),
    (0x0044d457, 'mov ecx, 0x113', 'filter: B of ship type 0x113 ignored unless A is type 0x113'),
    (0x0043fec5, 'cmp eax, dword ptr [edx + 0x38]', 'expendable: never the object at [*0x60850c+0x38]'),
    (0x0043fef8, 'mov ecx, 0x81f', 'expendable: KC class 0x81f'),
    (0x0043ff06, 'mov ecx, 0x812', 'expendable: KC class 0x812'),
    (0x0043ff16, 'mov ecx, 0x80d', 'expendable: KC class 0x80d'),
    (0x0043ff26, 'mov ecx, 0x7db', 'expendable: KC class 0x7db'),
    (0x0043ff36, 'mov ecx, 0x816', 'expendable: KC class 0x816'),
    (0x0043ff46, 'mov ecx, 0x810', 'expendable: KC class 0x810'),
    # Q3: the pass that sends CollisionWarn, and the pair loop
    (0x0045d844, 'call 0x44e450', 'pair loop: A = first object'),
    (0x0045d867, 'call 0x44e450', 'pair loop: A = second object'),
    (0x0045e05a, 'cmp word ptr [edx + 0x258], 0x12', 'L3: the only order excluded is 0x12'),
    (0x0045e06e, 'test ecx, 0x8000000', 'L3: object flag 0x8000000 -> no call'),
    (0x0045e079, 'push 0x55fd74', 'L3: "CollisionWarn"'),
    (0x0045e089, 'call 0x49f4c0', 'L3: synchronous KC call'),
    # Q5: the end of order 0x1e at a gate and the speed slew
    (0x00457c8b, 'jle 0x457c97', 'order 0x1e: |dz| <= r (large) or r/4 -> continue'),
    (0x00457c90, 'push 1', 'order 0x1e end: result 1 ...'),
    (0x00457c92, 'jmp 0x455ad0', '... shared end'),
    (0x00455ad2, 'mov word ptr [eax + 0x258], 0', 'shared end: order 0'),
    (0x00455adb, 'call 0x460560', 'shared end: wake the task'),
    (0x00455ae0, 'jmp 0x4593dd', 'shared end: site B, no speed write'),
    (0x00457d45, 'mov dword ptr [ebx + 0x14], eax', 'order 0x1e: desired = vmax every frame'),
    (0x00457d48, 'mov dword ptr [ebx + 0x10], 0xea60', 'order 0x1e: speed 60000 (large)'),
    (0x00457d5c, 'mov dword ptr [ebx + 0x10], 0x1d4c0', 'order 0x1e: speed 120000 (others)'),
    (0x004611f3, 'mov word ptr [ecx + 0x258], 0x1e', 'SA_ClearObjectFromHangar: order 0x1e'),
    (0x004594ef, 'call 0x450980', 'slew: desired capped at vmax'),
    (0x0045950d, 'mov eax, dword ptr [ecx + 0x278]', 'slew: desired capped at f+0x278'),
    (0x00459562, 'call 0x450b80', 'slew: step = a ...'),
    (0x00459569, 'imul ecx, ebx', '... * dt (ms) ...'),
    (0x0045956c, 'mov eax, 0x10624dd3', '... / 1000'),
    (0x004595a3, 'mov dword ptr [esi + 0x10], edx', 'slew: speed down toward desired'),
    (0x004595b1, 'mov dword ptr [esi + 0x10], eax', 'slew: speed up toward desired'),
]

TABLES = [  # (name, byte-table VA, count, first code, jump-table VA, {jump target: meaning}, expected {meaning: codes})
    ('capital 0x0043fd50 (SG_SH code)', 0x43fd8c, 14, 0, 0x43fd84, {0x43fd7e: 'true', 0x43fd81: 'false'},
     {'true': [0, 2, 3, 4, 8, 13]}),
    ('large 0x0043fd00 (SG_SH code)', 0x43fd3c, 16, 0, 0x43fd34, {0x43fd2e: 'true', 0x43fd31: 'false'},
     {'true': [0, 2, 3, 4, 8, 11, 13, 14, 15]}),
    ('obstacle filter 0x0044d2f0 (B class)', 0x44d494, 0x1d, 3, 0x44d48c, {0x44d326: 'tested', 0x44d483: 'ignored'},
     {'tested': [3, 4, 5, 6, 7, 0x11, 0x12, 0x14, 0x15, 0x1d, 0x1e, 0x1f]}),
    ('expendable 0x0043fe80 (class)', 0x43ff74, 11, 7, 0x43ff64,
     {0x43febf: 'ship: KC class', 0x43fe9e: 'true', 0x43fea1: 'class 10: not bit 0 of +0x43',
      0x43feab: 'class 0x11: +0x58 and subtype 9'},
     {'ship: KC class': [7], 'true': [8, 9, 11, 12, 13, 14, 15, 16], 'class 10: not bit 0 of +0x43': [10],
      'class 0x11: +0x58 and subtype 9': [0x11]}),
]

print('== EXE %s sha256 %s' % (X3 / 'X3AP.exe', hashlib.sha256(d).hexdigest()))
check(hashlib.sha256(d).hexdigest() == SHA, 'EXE SHA-256 = fdbf3418…')
for va, want, claim in INSNS:
    got = insn(va)
    check(got == want, '%08x %-42s %s%s' % (va, want, claim, '' if got == want else '   (got: %s)' % got))

print('\n== class tables')
for name, bt, n, first, jt, meaning, want in TABLES:
    idx = rd(bt, n)
    nj = max(idx) + 1
    targets = [u32(jt + 4 * k) for k in range(nj)]
    got = {}
    for k, b in enumerate(idx):
        got.setdefault(meaning.get(targets[b], hex(targets[b])), []).append(first + k)
    for m, codes in want.items():
        check(got.get(m) == codes, '%s: %s = %s' % (name, m, [hex(c) for c in got.get(m, [])]))

print('\n== direct calls (E8 rel32) of 0x0044d9c0 in .text')
callers = []
for k in range(nsec):
    o0 = pe + 24 + opt + 40 * k
    if d[o0:o0 + 8].rstrip(b'\0') != b'.text':
        continue
    vs, sva, rs, ra = secs[k]
    for i in range(rs - 5):
        if d[ra + i] == 0xe8 and base + sva + i + 5 + struct.unpack_from('<i', d, ra + i + 1)[0] == 0x44d9c0:
            callers.append(base + sva + i)
check(callers == [0x44d9f3, 0x44e2b5, 0x44e3ad, 0x456c9a], 'callers %s (recursion, warn x2, order 0x12)' % [hex(c) for c in callers])

print('\n== SA_ClearObjectFromHangar 0x004611a5..0x00461265: writes to obj+0x10 / obj+0x14')
wr = [i for i in md.disasm(rd(0x4611a5, 0x461265 - 0x4611a5), 0x4611a5)
      if i.mnemonic == 'mov' and re.match(r'dword ptr \[\w+ \+ 0x1[04]\],', i.op_str)]
check(not wr, 'no speed or desired-speed write at issue (%d writes)' % len(wr))

# ---------------------------------------------------------------- KC
G = Path(__file__).resolve().parent.parent / 'gates' / 'kc_gate_flow.py'
src = G.read_text()
g = {'__file__': str(G), '__name__': 'kc_gate_flow_defs'}
exec(compile(src[:src.index('\nMETHODS = ')], str(G), 'exec'), g)
Obj, member = g['Obj'], g['member']
o = Obj(member(X3 / 'addon/07.cat', 'L/x3story.obj'))
print('\n== KC addon/07.cat L/x3story.obj (Mayhem 4), CODE %d B' % len(o.code))


def lines_of(cid, nm):
    (e,) = o.find(cid, nm)
    return e, [l for _, _, l in o.track(e)[0]]


e, cw = lines_of(2004, 'CollisionWarn')
check(any('m47 & 524288' in l for l in cw) and any('m47 & 262144' in l for l in cw) and any('m48' in l for l in cw),
      '2004::CollisionWarn %05x: ignore = m47 & 0x80000, targeted = m47 & 0x40000 against m48, else SA_StopObject(ship, 2)' % e)
check(any('SA_StopObject(m3, 2)' in l for l in cw), '2004::CollisionWarn calls SA_StopObject(m3, 2)')
writers = []
for ent in sorted(o.meth):
    if not any(m[0] == 2004 for m in o.meth[ent]):
        continue
    for pc, _, l in o.track(ent)[0]:
        if re.match(r'm47 = ', l):
            writers.append(('%s' % [m[1] for m in o.meth[ent] if m[0] == 2004][0], pc, l))
for nm, pc, l in writers:
    print('   m47 writer: 2004::%s %05x %s' % (nm, pc, l))
names = {w[0] for w in writers}
check('__EscortObject' not in names and names <= {'__StartInHangar', '__LandInHangar', '__MoveToHangar', '__EnterHyperspace',
                                                  '__LeaveHyperspaceWarpIn', '__stopCommand', 'CallbackStopThisScript'},
      'm47 writers in class 2004: the hangar, gate and stop methods only (%d writes); none in __EscortObject' % len(writers))
e, eo = lines_of(2004, '__EscortObject')
k1 = next(i for i, l in enumerate(eo) if 'SA_CollisionObject(m3)' in l)
check(any('SA_AvoidObjectCollision(m3, (GetSize() + 50000))' in l for l in eo[k1:k1 + 6]),
      '2004::__EscortObject %05x: every loop pass tests SA_CollisionObject, then SA_AvoidObjectCollision(size + 50000)' % e)
check(any('SA_EscortObject(' in l for l in eo) and any('SA_FollowObject(' in l for l in eo),
      '2004::__EscortObject issues SA_EscortObject (0x14) or SA_FollowObject (0x15) after that test')
e, js = lines_of(2004, 'JumpToSector')
txt = '\n'.join(js)
check('call __LeaveHyperspace(p-4)' in txt, '2004::JumpToSector %05x: gate target -> __LeaveHyperspace(gate), the gate-transit arrival' % e)
check('call p-4.RequestWarpThroughSlot(this)' in txt and 'TI_Delay(5000)' in txt,
      '2004::JumpToSector: NPC 2019 to a gate requests the destination through slot, retry 5 s')
check('TI_Delay(16000)' in txt and 'SA_SetDesiredSpeed(m3, 0)' in txt,
      '2004::JumpToSector: desired speed 0 and a 16 s charge unless flags & 3')
check('call AddTo(p-4)' in txt and 'call 2004::SetPos(p-5, p-6, p-7)' in txt and 'call AddTo(p3.GetSector())' in txt,
      '2004::JumpToSector: sector or beacon target -> AddTo + SetPos (no gate launch)')
e, sp = lines_of(2004, 'SetPos')
check(any('SA_SetSafePos(m3, p-4, p-5, p-6)' in l for l in sp), '2004::SetPos %05x -> SA_SetSafePos' % e)
calls = []
for ent in sorted(o.meth):
    for pc, op in o.insns(ent):
        if op in (0x82, 0x85, 0x87) and o.name(struct.unpack_from('>i', o.code, pc + 1)[0]) == '__LeaveHyperspace':
            calls.append('%d::%s' % o.meth[ent][0][:2])
print('   __LeaveHyperspace callers: %s' % ', '.join(calls))
check('2004::JumpToSector' in calls and '2004::__FlyToNextSector' in calls, 'JumpToSector and __FlyToNextSector share __LeaveHyperspace')
e, wi = lines_of(2004, '__LeaveHyperspaceWarpIn')
k = next(i for i, l in enumerate(wi) if 'SA_ClearObjectFromHangar' in l)
check(wi[k + 1] == 'm47 = 0' and any('SA_AvoidObjectCollision(m3, 0)' in l for l in wi[k:]),
      '2004::__LeaveHyperspaceWarpIn %05x: after 0x1e: m47 = 0; SA_CollisionObject -> SA_AvoidObjectCollision(m3, 0)' % e)
check(any(l == 'm47 = 327684' for l in wi[:k]), '__LeaveHyperspaceWarpIn: m47 = 0x50004 (targeted, m48 = the gate) through 0x21/0x1e')

# ---------------------------------------------------------------- MSCI
S = X3 / 'addon/scripts'
MSCI = {
    'Mayhem.Task.TemplateWhenReady.xml': [r'call script Mayhem\.Squadron\.Add', r'Mayhem\.Squadron\.FindLeader'],
    'Mayhem.Squadron.Add.xml': [r'Mayhem\.SelectNewLeader', r'Mayhem\.Squadron\.SwitchLeader', r"'mayhem_squadronLeader' value = \$leader"],
    'Mayhem.SelectNewLeader.xml': [r'get hull$', r'\$ship\.hull > \$hull', r'get max speed', r'is of class \[TM\]'],
    'Mayhem.Squadron.SwitchLeader.xml': [r"'mayhem_squadronLeader' value = \$newLeader", r'START \$follower -> call named script'],
    'Mayhem.Squadron.SwitchLeaderIfNeeded.xml': [r'SelectNewLeader', r'START .*SwitchLeader'],
    'Mayhem.Task.Squadron.TaskForce.xml': [r'wait randomly from 3000 to 4000', r'SwitchLeaderIfNeeded'],
    'Mayhem.Task.Support.xml': [r'SwitchLeaderIfNeeded', r"ScriptName = 'Mayhem\.Squadron\.Add'"],
    'ZH.Monitor.xml': [r'SwitchLeaderIfNeeded'],
    'Mayhem.AssignNewLeader.xml': [r'SwitchLeader'],
    '!ship.signal.killed.xml': [r'Mayhem\.AssignNewLeader'],
    'Mayhem.Task.Squadron.Escort.xml': [r'wait randomly from 3000 to 4000', r'call script Mayhem\.Task\.Squadron\.Follower',
                                        r'find ship : sector = \[SECTOR\] class or type = \[Freighter\]', r'get destination',
                                        r"mayhem_escortLeader", r'!move\.follow : target = \$vip'],
    'Mayhem.Task.Squadron.Follower.xml': [r'Mayhem\.Squadron\.GetLeader', r'!move\.follow : target = \$leader'],
    'Mayhem.Squadron.GetLeader.xml': [r"'mayhem_squadronLeader'"],
    '!move.follow.template.xml': [r'is script Mayhem\.Task\.Squadron\.Follower on stack', r'Mayhem\.Squadron\.IsLeader : ship = \$leader',
                                  r'escort ship \$target : timeout', r'wait randomly from 2000 to 3000'],
    'Mayhem.Task.Move.xml': [r'^= wait 1 ms', r'wait randomly from 1000 to 2000', r'follow object \$nextGate', r'fly to sector \$nextSector'],
}
print('\n== MSCI loose scripts (%s)' % S)
for fn, pats in MSCI.items():
    raw = (S / fn).read_bytes()
    print('-- %s %d B sha256 %s' % (fn, len(raw), hashlib.sha256(raw).hexdigest()))
    s = raw.decode('latin1')
    body = s[s.find('<sourcetext'):s.find('</sourcetext>')]
    ls = [' '.join(html.unescape(re.sub(r'<[^>]+>', '', l)).split()) for l in re.findall(r'<line[^>]*>(.*?)</line>', body, re.S)]
    for p in pats:
        hit = [(k, t) for k, t in enumerate(ls) if not t.startswith('*') and re.search(p, t)]
        check(bool(hit), '%s /%s/ at %s' % (fn, p, [k for k, _ in hit]))
        for k, t in hit[:3]:
            print('     %3d %s' % (k, t[:150]))
wl = []
for p in sorted(S.glob('*.xml')):
    s = p.read_bytes().decode('latin1')
    if "mayhem_squadronLeader' value" in s or 'mayhem_squadronLeader&apos; value' in s or "mayhem_squadronLeader" in s:
        body = s[s.find('<sourcetext'):s.find('</sourcetext>')]
        for k, l in enumerate(re.findall(r'<line[^>]*>(.*?)</line>', body, re.S)):
            t = ' '.join(html.unescape(re.sub(r'<[^>]+>', '', l)).split())
            if re.search(r"set local variable : name = 'mayhem_squadronLeader' value", t) and not t.startswith('*'):
                wl.append('%s:%d' % (p.name, k))
print('   writers of mayhem_squadronLeader: %s' % ', '.join(wl))
check({w.split(':')[0] for w in wl} == {'Mayhem.Squadron.Add.xml', 'Mayhem.Squadron.SwitchLeader.xml'},
      'only Mayhem.Squadron.Add and Mayhem.Squadron.SwitchLeader write a squadron leader')

print('\nchecks %d, failures %d' % (checks, fails))
print('RESULT %s' % ('PASS' if fails == 0 else 'FAIL'))
