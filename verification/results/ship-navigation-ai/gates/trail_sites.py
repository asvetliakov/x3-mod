#!/usr/bin/env python3
"""Re-read the facts of docs/reverse-engineering/ship-navigation-ai.md section 10.12 (trail line questions of the
convoy gate transit design): the order-4 point reads and the container detour, the escort order's lost-leader end
and the leader's free path, order 0 and the slew loop, the SA wake and the scheduler pass, the owner group of a ship,
and the per-gate state of order 5/8.

1. EXE byte strings compared exactly (PE section map), plus the SA name/jump table entries used.
2. Sweeps over a linear capstone pass of .text:
   * immediate `or [reg+0x44], 4` and any and-mask on [reg+0x44] that clears bit 2 (value 4);
   * immediate 0x2000000 on [reg+0x40]: setters and readers;
   * non-indexed [reg+0x180] references in 0x00440000..0x0046a000 (type-table reads use an index register).
3. KC (x3story.obj, stock AP addon/04.cat and Mayhem 4 addon/07.cat) raw bytecode of 402::GetID, 2004::GetOwner,
   1000::GetEnvironment and the pseudo-statements of 2004::Remove, 2004::Deactivate, 2001::RemoveShip, plus every
   SA_SetGroup call (symbolic tracker of kc_gate_flow.py).
4. TShips column 10 (+0x50, the re-plan factor of f+0x20c = 50 * col10 ms) per OBJ_SHIP_* class, installed view.
Prints RESULT PASS only if every byte string, table entry, sweep expectation and KC expectation holds.

    python3 -I verification/results/ship-navigation-ai/gates/trail_sites.py [X3 dir] > trail_sites_out.txt
"""
import collections, re, statistics, struct, sys
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

X3 = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3')
SHA = 'fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab'
d = (X3 / 'X3AP.exe').read_bytes()
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


BYTES = [
    # item 2: order 4 (handler 0x4549c2) reads f+0x240 live at every re-plan tick; nothing between ticks
    (0x004549e3, '2b 86 10 02 00 00 0f 88 ee 49 00 00', 'order 4: now - f+0x210 < 0 -> jmp 0x4593dd (whole handler skipped between ticks)'),
    (0x004549ef, '8b b6 0c 02 00 00', 'order 4: interval f+0x20c ...'),
    (0x004549fe, 'e8 bd 97 09 00', '... rand(interval) 0x4ee1c0 ...'),
    (0x00454a17, '89 90 10 02 00 00', '... f+0x210 = now + rand(f+0x20c)'),
    (0x004404a1, '6b c9 32 89 8a 0c 02 00 00', 'ship init: f+0x20c = type[+0x50] * 50 ms'),
    (0x00454a20, '83 b8 50 02 00 00 00 7c 5d', 'order 4: port f+0x250 < 0 -> 0x454a86 (point path)'),
    (0x00454a86, '8b 88 40 02 00 00 8b 90 44 02 00 00', 'order 4, port < 0: x, y read from f+0x240/+0x244 ...'),
    (0x00454a99, '8b 88 48 02 00 00', '... z from f+0x248 into the stack local [esp+0x360] (rebuilt every tick)'),
    (0x00454acf, 'e8 bc d9 fb ff', 'order 4: 0x412490 (P - S)'),
    (0x00454b02, 'e8 79 50 01 00', 'order 4: 0x469b80 |P - S| -> [esp+0x2c8]'),
    (0x00454b09, '0f b7 47 48 66 3d 12 00', 'order 4: target class 0x12 (gate) ...'),
    (0x00454b1a, '8b 87 a4 00 00 00 8d 04 40 99 83 e2 03 03 c2 c1 f8 02', '... arrival radius 3*r_gate/4'),
    (0x00454b70, '3b f0 7f 2d', 'order 4: arrival iff |P - S| <= radius, before the detour and the leaf'),
    (0x00454b91, '89 43 14 89 43 10', 'order 4 arrival (P2): desired and current speed 0'),
    (0x00454c2c, 'db 44 24 10 dc 1d d8 54 56 00', 'detour a: u.(P - C) > 0'),
    (0x00454c48, 'ba 00 00 02 00', 'detour b: |P - S| < 2.0 * r_C'),
    (0x00454c6f, 'ba cc 0c 01 00', 'detour c: |C - S| > 1.05 * |P - C|'),
    (0x00454cef, 'e8 7c 34 03 00', 'detour d: r_C = 0x488170(container)'),
    (0x00454d0a, 'b8 67 66 66 66', 'detour e: |C - S| > r_C/20'),
    (0x00454ed5, 'e8 96 aa fd ff', 'detour: 0x42f970 sets the tangent length to r_C'),
    (0x00454efc, '89 8c 24 60 03 00 00', 'detour: P\' written to the stack local [esp+0x360] only'),
    (0x00454f81, '50 53 e8 98 80 ff ff', 'order 4: leaf 0x44d020(obj, &[esp+0x360]) after the arrival test'),
    (0x00488176, '8b 87 a4 00 00 00 85 c0', '0x488170: r = [obj+0xa4] when >= 0'),
    (0x0042f970, '51 8b 06 85 c0', '0x42f970: in-place set-length (ESI = vector, [esp+4] = length)'),
    (0x0046537c, '89 91 40 02 00 00', 'SA_FlyObjectBeforeContainerPos: f+0x240 = x (raw cell, world units)'),
    (0x00465388, '89 91 44 02 00 00', '... f+0x244 = y'),
    (0x00465394, '89 91 48 02 00 00', '... f+0x248 = z'),
    (0x0046539d, '66 c7 81 58 02 00 00 04 00', '... order 4'),
    (0x004653ac, '89 82 24 02 00 00', '... f+0x224 = container'),
    (0x004653b9, 'c7 80 50 02 00 00 ff ff ff ff', '... port f+0x250 = -1; no f+0x230, f+0x210 or speed write'),
    (0x004653a9, '8b 4d 08', '... [obj+0x7c] = dispatcher arg 1 (the caller\'s completion slot)'),
    # item 3: order 0x14 lost-leader end and the leader's free path
    (0x0045820d, '8b 86 24 02 00 00 85 c0', 'order 0x14: leader = f+0x224 ...'),
    (0x0045821c, '0f 84 ae 08 00 00 3b d8 0f 84 a6 08 00 00', '... null or self -> 0x458ad0'),
    (0x0045822a, '8b 40 54 85 c0 0f 84 9b 08 00 00 66 83 78 48 01 0f 85 90 08 00 00',
     '... no parent [leader+0x54], or parent class != 1 (not a sector) -> 0x458ad0'),
    (0x00458252, '0f 88 51 08 00 00', '... then the re-plan gate (the test above runs every frame)'),
    (0x00458ad0, '8b 53 50 6a 03 8b f3 66 c7 82 58 02 00 00 00 00 e8 7b 7a 00 00', 'lost leader: order 0, wake with 3'),
    (0x00458aac, '83 b8 8c 02 00 00 00', 'order 0x14: timeout armed f+0x28c ...'),
    (0x00455ac8, '0f 88 0f 39 00 00 6a 0d', '... now >= f+0x290: order 0, wake with 13'),
    (0x004618ef, '89 81 24 02 00 00', 'SA_EscortObject: f+0x224 = leader object pointer'),
    (0x0046074a, '6a 01 8b c8 e8 ed 00 fd ff', 'SA_FreeObject: 0x430840(obj, 1) at once'),
    (0x0043084b, 'e8 50 f4 00 00', '0x430840: 0x43fca0 (teardown) ...'),
    (0x0043fcc3, 'e8 c8 fc ff ff', '0x43fca0: 0x43f990'),
    (0x0043f9e1, '39 5d 7c 74 09 6a 08 8b f5 e8 71 0b 02 00', 'teardown: a waiting task of the freed object is woken with 8'),
    (0x0043fb24, '53 8b cd e8 84 af 00 00', 'teardown: 0x44aab0 clears every reference to the object'),
    (0x0044aad4, 'e8 e7 fc ff ff', '0x44aab0: 0x44a7c0 per listed object (recursive)'),
    (0x0044a7cb, '33 ed', '0x44a7c0: ebp = 0'),
    (0x0044a7fd, '0f bf 47 48 83 f8 13', '0x44a7c0: switch on the class word'),
    (0x0044a88d, '39 98 24 02 00 00 75 06 89 a8 24 02 00 00', 'class 7: f+0x224 == freed object -> 0'),
    # item 4: order 0 and the slew loop
    (0x00458f8e, 'f7 43 40 00 00 08 00 74 16 33 ff f6 43 44 04 75 03 89 7b 14 89 be 24 02 00 00 e9 33 01 00 00',
     'order 0, autopilot: [obj+0x14] = 0 unless [obj+0x44] & 4; f+0x224 = 0; every frame (no re-plan gate)'),
    (0x00459136, '89 8a 10 02 00 00 e9 9c 02 00 00', 'order 0 tail: f+0x210 = now; jmp 0x4593dd (site B)'),
    (0x004594ca, 'f7 46 40 00 02 80 00 0f 85 dd 00 00 00', 'slew: [obj+0x40] & 0x800200 -> no slew at all'),
    (0x004594d7, 'f7 46 44 00 00 00 02 0f 85 d0 00 00 00', 'slew: [obj+0x44] & 0x2000000 -> no slew at all'),
    (0x004594ef, 'e8 8c 74 ff ff 39 46 14 7e 0a', 'slew: desired > vmax ...'),
    (0x0045950d, '8b 81 78 02 00 00 85 c0 7c 08 39 46 14 7e 03 89 46 14', 'slew, class 7: desired = min(desired, f+0x278) when >= 0'),
    (0x00459533, 'a9 00 00 00 10 75 13', 'slew, class 7: carrier freeze test'),
    (0x00466df1, 'e8 ba 42 fd ff', 'SA_DecoupleShips: 0x43b0b0 (sets [obj+0x44] |= 4)'),
    (0x00466e37, 'e8 c4 4c fd ff', 'SA_StartInGameScene: 0x43bb00 (sets bit 4)'),
    (0x00466e54, 'e8 77 54 fd ff', 'SA_StartInGameScene2: 0x43c2d0 (sets bit 4)'),
    # item 5: the wake of a KC task and the scheduler pass
    (0x00460560, '8b 46 7c 85 c0 74 42 8b 0d e4 85 60 00', '0x460560: slot = [obj+0x7c] ...'),
    (0x0046057c, '8d 04 40 8b 44 c1 34', '... completion = [VM + 0x34 + slot*0x18] ...'),
    (0x00460587, '8b 4c 24 04 8b 96 80 00 00 00 51 52 ff d0', '... (task id [obj+0x80], result)'),
    (0x0049cf2e, 'c7 44 ce 34 10 49 4a 00', 'VM constructor: own native group completion = 0x4a4910 ...'),
    (0x0049cf72, '89 86 54 14 00 00', '... [VM+0x1454] = that group index'),
    (0x004a38ff, '8b 82 54 14 00 00 56 50 ff d1', 'interpreter: every native gets (ctx = [VM+0x1454], task, ...)'),
    (0x004a4910, '53 8b 1d e4 85 60 00 8b 03', '0x4a4910: task looked up in the VM task hash [VM+0]'),
    (0x004a494b, '66 c7 46 20 01 00', '0x4a4910: task+0x20 = 1 (continue)'),
    (0x004a4968, 'e8 d3 fd ff ff', '0x4a4910: 0x4a4740 (make runnable)'),
    (0x004a4740, '66 83 7e 3a 02', '0x4a4740: only a suspended task (state 2) ...'),
    (0x004a47b5, '66 c7 46 3a 01 00', '... state 1 ...'),
    (0x004a47c9, '8b 90 d8 12 00 00 05 d8 12 00 00 89 16', '... linked at the head of the runnable list VM+0x12d8'),
    (0x0049f777, '8b 86 d8 12 00 00 89 86 34 14 00 00', 'scheduler 0x49f770 runs the runnable list from its head'),
    (0x00403aff, 'e8 6c bc 09 00', 'main loop: scheduler pass ...'),
    (0x00403b17, 'e8 44 68 03 00', '... then the sector driver 0x43a360 (order handlers)'),
    (0x004041d8, '0f 84 d2 f8 ff ff', 'main loop back edge -> 0x403ab0'),
    (0x004039ab, 'e8 50 38 09 00', 'the fourth scheduler caller 0x497200 is called once, before the loop'),
    (0x0045520a, '2b 8e 14 02 00 00 78 2a', 'order 8: now >= f+0x214 ...'),
    (0x00455228, '6a 01 8b f3 89 82 18 02 00 00 e8 29 b3 00 00', '... order 0, wake with 1'),
    # item 8: owner group
    (0x00462bc9, 'e8 62 db fe ff', 'SA_SetGroup: 0x450730(obj, group)'),
    (0x00450730, '66 3d 0a 00 56 8b f1 66 89 46 3c', '0x450730: [obj+0x3c] = group (word); group 10 special-cased'),
    (0x00462bf3, '0f bf 50 3c', 'SA_GetGroup: movsx [obj+0x3c]'),
    (0x0044001a, '83 c8 ff 66 89 45 3c', 'object init: [obj+0x3c] = -1'),
    (0x0044ada1, None, 'SA_SetAsPlayerObject: [obj+0x40] |= 0x40002 (bytes checked by stageb/arrival scripts)'),
    # item 1: per-gate state of order 5/8
    (0x004612a7, '66 c7 82 58 02 00 00 08 00', 'SA_WarpObjectAway: order 8 (no test of any gate state before it)'),
    (0x004612cc, '81 c1 84 03 00 00 89 8a 14 02 00 00', '... f+0x214 = now + 900'),
    (0x00461302, '66 83 78 48 12 75 10 81 48 40 00 00 00 02 81 4e 40 00 04 00 00',
     '... gate class 0x12: [gate+0x40] |= 0x2000000, [ship+0x40] |= 0x400'),
    (0x00461322, '89 82 24 02 00 00', '... f+0x224 = gate'),
    (0x00455252, '81 48 40 00 00 00 02', 'order 8 per frame: [gate+0x40] |= 0x2000000'),
    (0x004548cf, '81 48 40 00 00 00 02', 'order 5 aligned: [gate+0x40] |= 0x2000000'),
    (0x0044b124, 'f7 46 40 00 00 00 02', 'gate post-pass 0x44b0c0: the only reader of 0x2000000 on a gate ...'),
    (0x0044b134, '8b 91 80 01 00 00 2b d3 0f 89 cd 01 00 00', '... runs the part animation only when [gate.f+0x180] < now'),
    (0x0044b30f, '8b 91 80 01 00 00 2b d3 85 d2', '... second animation branch on [gate.f+0x180]'),
    (0x0044b155, 'e8 86 fd ff ff', '... part animation 0x44aee0'),
    (0x004441c3, '89 90 80 01 00 00', 'arrival launch 0x444170: [gate.f+0x180] = now + 10000'),
    (0x0045b741, 'bd ff ff ff ed', 'post pass: mask 0xedffffff clears 0x2000000 and 0x10000000 every frame'),
]
SA = {0x01: ('SA_FreeObject', 0x00460736), 0x0c: ('SA_FlyObjectBeforeContainerPos', 0x0046533c),
      0x0e: ('SA_WarpObjectAway', 0x0046126a), 0x14: ('SA_EscortObject', 0x004618a6),
      0x6f: ('SA_SetGroup', 0x00462bb0), 0x70: ('SA_GetGroup', 0x00462bdb), 0x11e: ('SA_SetAsPlayerObject', 0x00463de6),
      0x158: ('SA_DecoupleShips', 0x00466dcd), 0x159: ('SA_StartInGameScene', 0x00466e12),
      0x15a: ('SA_StartInGameScene2', 0x00466e49)}

bad = 0
import hashlib
sha = hashlib.sha256(d).hexdigest()
print('X3AP.exe SHA-256 %s %s' % (sha, 'OK' if sha == SHA else 'MISMATCH')); bad += sha != SHA
print('\n1. byte strings')
for va, exp, why in BYTES:
    if exp is None:
        continue
    got = rd(va, len(bytes.fromhex(exp))).hex(' ')
    ok = got == exp; bad += not ok
    print('  %s %08x %-40s %s' % ('ok ' if ok else 'BAD', va, exp[:40], why))
    if not ok: print('      got %s' % got)
for cid, (nm, case) in sorted(SA.items()):
    n, c = cstr(u32(0x0057a7f0 + 4 * cid)), u32(0x004690f0 + 4 * cid)
    ok = n == nm and c == case; bad += not ok
    print('  %s SA 0x%03x %s -> %08x' % ('ok ' if ok else 'BAD', cid, n, c))
bt, jt = rd(0x0044aa9c, 0x14), [u32(0x0044aa78 + 4 * k) for k in range(9)]
ok = jt[bt[7]] == 0x0044a88d - 3; bad += not ok
print('  %s 0x44a7c0 class 7 -> %08x (f+0x224 clear)' % ('ok ' if ok else 'BAD', jt[bt[7]]))

print('\n2. sweeps')
md = Cs(CS_ARCH_X86, CS_MODE_32); md.skipdata = True
lo, hi = 0x00401000, 0x00560000
ins = list(md.disasm(rd(lo, hi - lo), lo))
set44 = [i.address for i in ins if i.mnemonic == 'or' and re.fullmatch(r'dword ptr \[e\w\w \+ 0x44\], 4', i.op_str)]
clr44 = [i.address for i in ins if i.mnemonic == 'and' and re.fullmatch(r'(dword|byte) ptr \[e\w\w \+ 0x44\], (0x[0-9a-f]+)', i.op_str)
         and not int(i.op_str.split(', ')[1], 16) & 4]
ok = set44 == [0x0043b216, 0x0043b8a2, 0x0043c048, 0x0043c3bd] and clr44 == []; bad += not ok
print('  %s [obj+0x44] |= 4 setters %s; and-masks clearing bit 2: %s' % ('ok ' if ok else 'BAD', [hex(a) for a in set44], [hex(a) for a in clr44]))
s40, r40 = [], []
for k, i in enumerate(ins):
    if not i.op_str.endswith('0x2000000'):
        continue
    if i.mnemonic == 'or' and '+ 0x40]' in i.op_str: s40.append(i.address)
    elif i.mnemonic == 'test' and '+ 0x40]' in i.op_str: r40.append(i.address)
    elif i.mnemonic == 'test' and i.op_str.startswith('e'):
        reg = i.op_str.split(',')[0]
        if any(p.mnemonic == 'mov' and p.op_str.startswith(reg + ',') and '+ 0x40]' in p.op_str for p in ins[max(0, k - 8):k]):
            r40.append(i.address)
ok = s40 == [0x0045481f + 0xb0, 0x00455252, 0x0045d811, 0x00461309] and r40 == [0x0044b124]; bad += not ok
print('  %s 0x2000000 on [reg+0x40]: setters %s, readers %s' % ('ok ' if ok else 'BAD', [hex(a) for a in s40], [hex(a) for a in r40]))
r180 = [i.address for i in ins if 0x00440000 <= i.address < 0x0046a000 and re.search(r'\[e\w\w \+ 0x180\]', i.op_str)]
print('  non-indexed [reg+0x180] in 0x440000..0x46a000: %s' % [hex(a) for a in r180])
ok = {0x0044b134, 0x0044b30f}.issubset(r180) and 0x004441c3 in r180; bad += not ok
print('  %s gate f+0x180: launch write 0x4441c3, readers 0x44b134/0x44b30f (the others are other objects\' f+0x180, '
      'listed for review)' % ('ok ' if ok else 'BAD'))

print('\n3. KC')
src = (Path(__file__).resolve().parent / 'kc_gate_flow.py').read_text()
g = {'__file__': str(Path(__file__).resolve().parent / 'kc_gate_flow.py'), '__name__': 'kc_gate_flow_defs'}
exec(compile(src[:src.index('\nMETHODS = ')], 'kc_gate_flow.py', 'exec'), g)
for cat, label in ((X3 / 'addon/04.cat', 'stock AP'), (X3 / 'addon/07.cat', 'Mayhem 4')):
    o = g['Obj'](g['member'](cat, 'L/x3story.obj'))
    print('  == %s (%s)' % (cat.relative_to(X3), label))
    raw = {k: o.code[o.find(*k)[0]:o.find(*k)[0] + 6].hex(' ') for k in ((402, 'GetID'), (2004, 'GetOwner'), (1000, 'GetEnvironment'))}
    ok = raw[(402, 'GetID')] == '6e 00 01 05 0a 83' and raw[(1000, 'GetEnvironment')] == '6e 00 01 0f 00 01'
    ok = ok and raw[(2004, 'GetOwner')][:12] == '6e 00 01 0f '; bad += not ok
    print('  %s 402::GetID %s (push 10), 1000::GetEnvironment %s (m1), 2004::GetOwner %s (a member)'
          % ('ok ' if ok else 'BAD', raw[(402, 'GetID')], raw[(1000, 'GetEnvironment')], raw[(2004, 'GetOwner')]))
    for cid, nm in ((2004, 'Remove'), (2004, 'Deactivate'), (2001, 'RemoveShip')):
        lines, conf = o.track(o.find(cid, nm)[0])
        calls = [l for _, _, l in lines if l.startswith('call')]
        print('    %d::%s: %s' % (cid, nm, ' | '.join(c[5:] for c in calls)))
        if cid == 2004 and nm == 'Deactivate':
            ok = any('SA_StopObject' in c for c in calls) and any('SA_FreeObject' in c for c in calls); bad += not ok
        if cid == 2001:
            ok = any('SetEnvironment(0)' in c for c in calls); bad += not ok
    n = 0
    for e in sorted(o.meth):
        if not any(op in (0x82, 0x85, 0x87) and o.name(struct.unpack_from('>i', o.code, pc + 1)[0]) == 'SA_SetGroup'
                   for pc, op in o.insns(e)):
            continue
        for _, _, l in o.track(e)[0]:
            if l.startswith('call SA_SetGroup('):
                print('    %d::%s %s' % (o.meth[e][0][0], o.meth[e][0][1], l[5:])); n += 1
    ok = n == 5; bad += not ok
    print('  %s SA_SetGroup call sites: %d (all pass the owner id)' % ('ok ' if ok else 'BAD', n))

print('\n4. TShips column 10 (type[+0x50]) per class, installed view')
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'tools/analysis'))
from sector_fog_census import Assets  # noqa: E402
data, prov = Assets(X3).get('types/TShips.txt')
print('  %s %s decoded SHA-256 %s' % (prov['source'], prov['member'], prov['decoded_sha256']))
by = collections.defaultdict(list)
for line in data.decode('latin1').splitlines()[1:]:
    c = line.split(';')
    if len(c) > 52 and not line.lstrip().startswith('/'):
        by[c[52].strip()].append(int(c[10]))
for k in ('OBJ_SHIP_M0', 'OBJ_SHIP_M1', 'OBJ_SHIP_M2', 'OBJ_SHIP_M7', 'OBJ_SHIP_TL', 'OBJ_SHIP_M6', 'OBJ_SHIP_TS', 'OBJ_SHIP_M3'):
    v = by.get(k, [])
    print('  %-12s n=%3d min %d median %s max %d -> f+0x20c = %s ms' % (k, len(v), min(v), statistics.median(v), max(v),
                                                                     sorted({50 * x for x in v})))
ok = all(set(by[k]) == {1} for k in ('OBJ_SHIP_M1', 'OBJ_SHIP_M2', 'OBJ_SHIP_M7', 'OBJ_SHIP_TL')); bad += not ok
print('  %s every M1/M2/M7/TL row has col10 = 1 (re-plan every rand(50) ms)' % ('ok ' if ok else 'BAD'))
print('\nRESULT %s (%d failures)' % ('PASS' if bad == 0 else 'FAIL', bad))
