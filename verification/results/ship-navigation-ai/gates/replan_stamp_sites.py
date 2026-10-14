#!/usr/bin/env python3
"""Re-read the facts of docs/reverse-engineering/ship-navigation-ai.md section 10.13 (the re-plan stamp f+0x210 and the
lane escort's tick deferral at site B).

1. EXE byte strings compared exactly (PE section map), plus the SA name/case table entries used.
2. A linear capstone pass of .text (skipdata) listing every non-indexed [reg+0x210] memory operand, classified as
   flight-record writer or reader by an expected table (SA case, order handler, ship init / load / save, launch paths,
   and the non-flight structures: cockpit record, B3D node, stack locals), plus the indexed [reg+idx+0x210] operands
   (none reaches f+0x210). Fails on any site not in the tables.
3. The SA cases that can follow order 0 for an escort: whether each writes f+0x210 at issue (sites inside the case's
   own code range, from the SA case table).
Prints RESULT PASS only if every byte string, table entry and sweep expectation holds.

    python3 -I verification/results/ship-navigation-ai/gates/replan_stamp_sites.py [X3 dir] > replan_stamp_sites_out.txt
"""
import hashlib, re, struct, sys
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone import x86

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
    # the clock: every handler and site B read [[0x606f34]+0x718]
    (0x004549d7, '8b 15 34 6f 60 00 8b 82 18 07 00 00', 'order 4: now = [[0x606f34]+0x718] (the clock site B reads)'),
    (0x004549c2, '8b be 24 02 00 00 85 ff', 'order 4 head: target f+0x224 ...'),
    (0x004549d1, '0f 84 2e 07 00 00', '... null -> 0x455105 (port -1, order 0, wake 3): the only test before the gate'),
    (0x004549e3, '2b 86 10 02 00 00 0f 88 ee 49 00 00', 'order 4: sub now - f+0x210; js 0x4593dd (signed, modular)'),
    (0x004549ef, '8b b6 0c 02 00 00 85 f6 7f 04 33 c0', 'order 4 tick: n = f+0x20c; n <= 0 -> 0 ...'),
    (0x00454a12, '03 d0 8b 43 50 89 90 10 02 00 00', '... f+0x210 = now + rand(n)'),
    (0x00454b70, '3b f0 7f 2d', 'order 4: arrival test, after the gate'),
    (0x0045820d, '8b 86 24 02 00 00 85 c0', 'order 0x14 head: leader tests (every frame) ...'),
    (0x0045824c, '2b 96 10 02 00 00 0f 88 51 08 00 00', '... sub now - f+0x210; js 0x458aa9'),
    (0x00458aa9, '8b 43 50 83 b8 8c 02 00 00 00 0f 84 24 09 00 00', '0x14 skip: timeout f+0x28c/f+0x290 test, else site B'),
    (0x00458aea, '8b 86 24 02 00 00 85 c0', 'order 0x15 head: target tests (every frame) ...'),
    (0x00458b28, '2b 8e 10 02 00 00 0f 88 72 cf ff ff', '... sub now - f+0x210; js 0x455aa6'),
    (0x00455aa6, '8b 43 50 83 b8 8c 02 00 00 00 0f 84 27 39 00 00', '0x15 skip: timeout test (shared), else site B'),
    (0x00455ac8, '0f 88 0f 39 00 00 6a 0d', '... now - f+0x290 < 0 -> site B; else order 0, wake 13'),
    (0x00458f8e, 'f7 43 40 00 00 08 00 74 16', 'order 0: autopilot bit 0x80000 ...'),
    (0x00458fa8, 'e9 33 01 00 00', '... autopilot path -> tail 0x4590e0'),
    (0x004590de, '33 ff 8b 53 50', 'order 0: non-autopilot path falls into the tail'),
    (0x00459119, '89 b8 18 02 00 00 8b 4b 50 89 b9 8c 02 00 00', 'order 0 tail: f+0x218 = 0, f+0x28c = 0 ...'),
    (0x00459128, 'a1 34 6f 60 00 8b 53 50 8b 88 18 07 00 00 89 8a 10 02 00 00 e9 9c 02 00 00',
     '... f+0x210 = now; jmp 0x4593dd (site B), nothing in between'),
    (0x004593dd, '8b 1b 83 3b 00', 'site B (ship_steer_loop_continue)'),
    (0x00452adc, 'a1 34 6f 60 00 8b 80 14 07 00 00 3d e8 03 00 00', 'interpreter dt = [+0x714] clamped to 1000 (integration only)'),
    (0x004404a1, '6b c9 32 89 8a 0c 02 00 00', 'ship init: f+0x20c = type[+0x50] * 50'),
    (0x004404ad, '8b 88 0c 02 00 00 8b 15 34 6f 60 00 03 8a 18 07 00 00 53 89 88 10 02 00 00',
     'ship init: f+0x210 = now + f+0x20c'),
    (0x0046537c, '89 91 40 02 00 00', 'SA_FlyObjectBeforeContainerPos: f+0x240 = x (no f+0x210 write in the case)'),
    (0x004653b9, 'c7 80 50 02 00 00 ff ff ff ff', '... port -1'),
    (0x00461925, '8b 15 34 6f 60 00 8b 82 18 07 00 00 8b 4e 50 89 81 10 02 00 00', 'SA_EscortObject: f+0x210 = now'),
    (0x004619ca, 'a1 34 6f 60 00 8b 88 18 07 00 00 8b 56 50 89 8a 10 02 00 00', 'SA_FollowObject: f+0x210 = now'),
    (0x00460c84, '8b 0d 34 6f 60 00 8b 43 50 8b 91 18 07 00 00 8b 4c 24 2c 89 90 10 02 00 00',
     'SA_FlyObjectIntoContainer: f+0x210 = now'),
    (0x00460e7d, '8b 15 34 6f 60 00 8b 4b 50 8b 82 18 07 00 00 89 81 10 02 00 00',
     'SA_FlyObjectBeforeContainer: f+0x210 = now'),
    (0x00461f39, 'a1 34 6f 60 00 8b 56 50 8b 88 18 07 00 00 89 8a 10 02 00 00', 'SA_StopObject: f+0x210 = now'),
    (0x00461a95, '8b 0d 34 6f 60 00 8b 91 18 07 00 00 8b 4d 08 03 d0 8b 46 50 89 90 10 02 00 00',
     'SA_FollowObjectByCut: f+0x210 = now + rand'),
    (0x00463c16, '81 c2 60 ea 00 00 89 90 10 02 00 00', 'SA_SetPlayerLand: f+0x210 = now + 60000'),
    (0x004321ec, '8b 95 0c 02 00 00 52 56 e8 a7 72 0b 00 8b 85 10 02 00 00 50 56 e8 9a 72 0b 00',
     'save 0x4310a0 (class 7, SOBJ): writes f+0x20c, f+0x210 raw'),
    (0x00431494, '89 95 10 02 00 00', 'load 0x4310a0: f+0x210 = the saved value (byte-swapped read, no rebase)'),
    (0x00441f8f, '8b 53 50 53 6a 01 52 8b c6 e8 03 f1 fe ff', 'SOBJ writer 0x441e10, class 7: 0x4310a0(flight, 1, obj)'),
    (0x004047ed, 'a1 34 6f 60 00 8b 88 18 07 00 00', 'save routine: the clock [+0x718] is written'),
    (0x00404f75, '8b 15 34 6f 60 00 89 82 18 07 00 00', 'load 0x404cc0: the clock [+0x718] is restored'),
    (0x004d1f30, '81 fe c8 00 00 00 7e 08', 'clock 0x4d1df0: real step clamped to <= 200 ms ...'),
    (0x004d1f5c, '01 81 18 07 00 00', '... [+0x718] += step * ([+0xcc]*[+0xd0] 16.16, the time factor)'),
    (0x004ee1ec, '8b 8a 18 07 00 00 8b c1 2b 05 04 91 60 00', '0x4ee1e0: [+0x714] = delta of [+0x718] (the frame step) ...'),
    (0x004ee216, '89 82 14 07 00 00', '... stored'),
    (0x0044412f, '8b 0d 34 6f 60 00 8b 43 50 8b 91 18 07 00 00 89 90 10 02 00 00', 'SA_StartInHangar 0x443920: f+0x210 = now'),
    (0x004448e8, 'a1 34 6f 60 00 8b 53 50 8b 88 18 07 00 00 89 8a 10 02 00 00', 'arrival launch 0x444170: f+0x210 = now'),
    (0x0045b797, 'e8 d4 89 fe ff', 'post pass calls the arrival launch 0x444170'),
]
SA = {0x09: ('SA_FlyObjectIntoContainer', 0x0046082e), 0x0a: ('SA_FlyObjectBeforeContainer', 0x00460cae),
      0x0c: ('SA_FlyObjectBeforeContainerPos', 0x0046533c), 0x0d: ('SA_ClearObjectFromHangar', 0x0046117e),
      0x0e: ('SA_WarpObjectAway', 0x0046126a), 0x0f: ('SA_AvoidObjectCollision', 0x0046104c),
      0x10: ('SA_CaptureObject', 0x0046142f), 0x11: ('SA_AttackObject', 0x004614da), 0x12: ('SA_AttackObjectBig', 0x004616e0),
      0x13: ('SA_DefensiveMove', 0x004617cf), 0x14: ('SA_EscortObject', 0x004618a6), 0x16: ('SA_TurnToObject', 0x00461fdd),
      0x19: ('SA_StartInHangar', 0x004626e2), 0x1a: ('SA_Idle', 0x0046274e),
      0x1b: ('SA_FollowObject', 0x0046196f), 0x1c: ('SA_FollowObjectByCut', 0x00461a13), 0x1d: ('SA_RotateToAngles', 0x004653d1),
      0x1e: ('SA_MovePos', 0x0046232c), 0x1f: ('SA_MovePosContinue', 0x0046239c), 0x20: ('SA_GlidePos', 0x004656f3),
      0x21: ('SA_WarpObjectIn', 0x00465b22), 0xfa: ('SA_StopObject', 0x00461e89), 0x119: ('SA_SetPlayerLand', 0x00463bc4),
      0x177: ('SA_CollectDebris', 0x00468c96)}

# every non-indexed [reg+0x210] operand in .text: address -> (kind, owner)
FLIGHT_W = {
    0x004404c0: 'ship init 0x43ffa0: now + f+0x20c', 0x00431494: 'load 0x4310a0: saved value',
    0x00443e4f: 'SA_StartInHangar -> 0x443920: now', 0x0044413e: 'SA_StartInHangar -> 0x443920: now',
    0x004448f6: 'arrival launch 0x444170 (post pass 0x45b797): now',
    0x00460c97: 'SA 0x09 FlyObjectIntoContainer: now', 0x00460e8c: 'SA 0x0a FlyObjectBeforeContainer: now',
    0x00461083: 'SA 0x0f AvoidObjectCollision: now', 0x004611ea: 'SA 0x0d ClearObjectFromHangar: now',
    0x0046148d: 'SA 0x10 CaptureObject: now', 0x0046165e: 'SA 0x11/0x12 AttackObject(Big) shared tail: now',
    0x00461839: 'SA 0x13 DefensiveMove: now', 0x00461934: 'SA 0x14 EscortObject: now', 0x004619d8: 'SA 0x1b FollowObject: now',
    0x00461aa9: 'SA 0x1c FollowObjectByCut: now + rand(1000)', 0x00461f47: 'SA 0xfa StopObject: now',
    0x004627b9: 'SA 0x1a Idle: now', 0x00463c1c: 'SA 0x119 SetPlayerLand: now + 60000', 0x00463c58: 'SA 0x119 SetPlayerLand (else): now',
    0x00468d79: 'SA 0x177 CollectDebris: now',
    0x00453d63: 'order 0x1d tick', 0x00454a17: 'order 4 tick', 0x0045551e: 'order 6 tick', 0x004556d5: 'order 0x22 tick',
    0x00455c3f: 'order 0x24 tick', 0x00455f09: 'order 0x10/0x11/0x18 tick', 0x00456513: 'order 0x19 tick',
    0x004565e2: 'order 0x23 tick', 0x0045679e: 'order 0x12 tick (now + 200)', 0x00458283: 'order 0x14 tick',
    0x00458b9e: 'order 0x15 tick', 0x00459136: 'order 0 tail: now',
}
FLIGHT_R = {
    0x00453d2d: 'order 0x1d gate', 0x004549e3: 'order 4 gate', 0x00455152: 'order 0x1b (landing clearance)',
    0x004554e7: 'order 6 gate', 0x004556a1: 'order 0x22 gate', 0x00455c04: 'order 0x24 gate', 0x00455ecb: 'order 0x10/0x11/0x18 gate',
    0x004564d9: 'order 0x19 gate', 0x004565a8: 'order 0x23 gate', 0x00456780: 'order 0x12 gate',
    0x00457f76: 'order 0x1e (time since issue)', 0x0045824c: 'order 0x14 gate', 0x00458b28: 'order 0x15 gate',
    0x004321f9: 'save 0x4310a0',
}
OTHER = {  # not the flight record (base register provenance)
    0x0041a381: 'cockpit record load 0x419610', 0x0041b638: 'cockpit record save 0x419610', 0x0041fd64: 'cockpit record init 0x41f8d0',
    0x0042d88c: 'script case on 0x41cd20 (cockpit)', 0x0042d8f1: 'script case on 0x41cd20 (cockpit)',
    0x00478b79: 'B3D node', 0x0047a278: 'B3D node', 0x004d4970: 'lea', 0x004d5b17: 'lea', 0x004d5ce4: 'lea',
}
INDEXED = {  # [reg+idx+0x210] operands
    0x00461157: 'SA_AvoidObjectCollision loop, index ecx = 0x30/0x34/0x38 (0x0046112e mov ecx, 0x30; 3 rounds): f+0x240..+0x248',
    0x004d5ccf: 'byte table [esi+ecx+0x210], ecx < 0xff (not a flight record)',
    0x004d5db0: 'byte table [esi+edi+0x210] (same structure as 0x4d5ccf)',
}
COCKPIT_FN = (0x00423960, 0x00424596)  # 0x423960 (called from 0x422ff0 on the [0x608504] cockpit record), ebx-based block

bad = 0
sha = hashlib.sha256(d).hexdigest()
print('X3AP.exe SHA-256 %s %s' % (sha, 'OK' if sha == SHA else 'MISMATCH')); bad += sha != SHA
print('\n1. byte strings')
for va, exp, why in BYTES:
    got = rd(va, len(bytes.fromhex(exp))).hex(' ')
    ok = got == exp; bad += not ok
    print('  %s %08x %-44s %s' % ('ok ' if ok else 'BAD', va, exp[:44], why))
    if not ok: print('      got %s' % got)
case_tab = [u32(0x004690f0 + 4 * i) for i in range(0x17a)]
for cid, (nm, case) in sorted(SA.items()):
    n, c = cstr(u32(0x0057a7f0 + 4 * cid)), case_tab[cid]
    ok = n == nm and c == case; bad += not ok
    print('  %s SA 0x%03x %s -> %08x' % ('ok ' if ok else 'BAD', cid, n, c))

print('\n2. sweep: every [reg+0x210] memory operand in .text')
md = Cs(CS_ARCH_X86, CS_MODE_32); md.skipdata = True; md.detail = True
lo, hi = 0x00401000, 0x00531630
hits, idx_hits = [], []
for i in md.disasm(rd(lo, hi - lo), lo):
    if i.id == 0:
        continue
    for o in i.operands:
        if o.type == x86.X86_OP_MEM and o.mem.disp == 0x210:
            (hits if o.mem.index == 0 else idx_hits).append(i)
unknown = []
w = r = 0
for i in hits:
    a = i.address
    if a in FLIGHT_W: tag = 'flight W  ' + FLIGHT_W[a]; w += 1
    elif a in FLIGHT_R: tag = 'flight R  ' + FLIGHT_R[a]; r += 1
    elif a in OTHER: tag = 'other     ' + OTHER[a]
    elif COCKPIT_FN[0] <= a < COCKPIT_FN[1]: tag = 'other     cockpit 0x423960'
    elif 'esp +' in i.op_str: tag = 'other     stack local'
    else: tag = 'UNKNOWN'; unknown.append(a)
    print('  %08x %-40s %s' % (a, (i.mnemonic + ' ' + i.op_str)[:40], tag))
missing = sorted(set(FLIGHT_W) | set(FLIGHT_R) | set(OTHER) - {x.address for x in hits})
missing = [a for a in missing if a not in {x.address for x in hits}]
for i in idx_hits:
    print('  %08x %-40s indexed: %s' % (i.address, (i.mnemonic + ' ' + i.op_str)[:40], INDEXED.get(i.address, 'UNKNOWN')))
idx_unknown = [i.address for i in idx_hits if i.address not in INDEXED]
ok = not unknown and not idx_unknown and not missing and rd(0x0046112e, 5).hex(' ') == 'b9 30 00 00 00'; bad += not ok
print('  %s %d operands: flight writers %d, flight readers %d; unknown %s; indexed unknown %s; expected but absent %s'
      % ('ok ' if ok else 'BAD', len(hits), w, r, [hex(a) for a in unknown], [hex(a) for a in idx_unknown],
         [hex(a) for a in missing]))

print('\n3. SA cases that can follow order 0 for an escort: f+0x210 written at issue?')
starts = sorted(set(case_tab))
def case_range(c):
    nxt = [s for s in starts if s > c]
    return c, (nxt[0] if nxt else 0x00469000)
writers = [a for a in FLIGHT_W if 0x00460630 <= a < 0x00469100]
EXPECT = {0x0c: False, 0x14: True, 0x1b: True, 0x1d: False, 0x09: True, 0x0a: True, 0x0e: False, 0xfa: True, 0x1a: True, 0x0f: True}
for cid, want in sorted(EXPECT.items()):
    a0, a1 = case_range(case_tab[cid])
    got = [hex(a) for a in writers if a0 <= a < a1]
    ok = bool(got) == want; bad += not ok
    print('  %s SA 0x%03x %-32s case %08x..%08x writes f+0x210 at %s' % ('ok ' if ok else 'BAD', cid, SA[cid][0], a0, a1, got or 'none'))
print('\nRESULT %s (%d failures)' % ('PASS' if bad == 0 else 'FAIL', bad))
