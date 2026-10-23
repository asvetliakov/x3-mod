#!/usr/bin/env python3
"""Manual flight input and SETA in X3AP.exe: every EXE fact quoted in docs/reverse-engineering/seta-manual-flight.md,
re-derived read-only from the installed files.

Checks: the nine input flags (every absolute writer and reader in .text, the records they belong to, the pointer-based
writers inside the input helpers, the absence of any other reader), the stop block and the one-byte patch at
0x0041211d (instruction boundaries, branch targets, alignment, register liveness, the decoded patched image), the
angular controllers 0x0040eb60/0x0040edc0/0x0040efd0 (clamp, ramp step from cfg+0x714, change threshold) and their
candidate hook spans, the manual-rotation integrator, strafe, speed keys, the look routine, the mouse-delta
normalisation, the game-step and cfg+0xcc readers of the input family, the warp-aware AI margins, the control names
of the key actions. Ends with RESULT PASS only if every check holds. Prints derived facts, no listings.

    python3 -I verification/results/seta-manual-flight/seta_manual_flight_study.py [X3 dir] > seta_manual_flight_study.txt
"""
import gzip, hashlib, re, struct, sys
from pathlib import Path
import capstone
from capstone import x86

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / 'tools/analysis'))
from inspect_x3 import read_catalogue        # noqa: E402

X3 = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3')
FAIL = []


def check(ok, what):
    print('   [%s] %s' % ('ok' if ok else 'FAIL', what))
    if not ok: FAIL.append(what)


# ---------------------------------------------------------------- image
EXE = X3 / 'X3AP.exe'
d = EXE.read_bytes()
sha = hashlib.sha256(d).hexdigest()
print('X3AP.exe SHA-256 %s' % sha)
check(sha.startswith('fdbf3418d8f0a897'), 'EXE identity')
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec, opt = struct.unpack_from('<H', d, pe + 6)[0], struct.unpack_from('<H', d, pe + 20)[0]
secs = [struct.unpack_from('<IIII', d, pe + 24 + opt + 40 * i + 8) for i in range(nsec)]


def off(va):
    r = va - 0x400000
    for vs, sva, rs, ra in secs:
        if sva <= r < sva + max(vs, rs): return ra + r - sva
    raise ValueError(hex(va))


rd = lambda va, n: d[off(va):off(va) + n]
cstr = lambda va: d[off(va):d.index(b'\0', off(va))].decode('latin1')
f32 = lambda va: struct.unpack('<f', rd(va, 4))[0]
f64 = lambda va: struct.unpack('<d', rd(va, 8))[0]
vs_, sva_, rs_, ra_ = [s for s in secs if s[1] == 0x1000][0]
TV, T = 0x400000 + sva_, d[ra_:ra_ + rs_]
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True
md.skipdata = True
INS = []   # (va, size, mnemonic, op_str, [(kind, base, index, disp/imm, access)])
for i in md.disasm(T, TV):
    if i.id == 0: continue
    ops = []
    for o in i.operands:
        if o.type == x86.X86_OP_MEM:
            ops.append(('m', i.reg_name(o.mem.base) if o.mem.base else None,
                        i.reg_name(o.mem.index) if o.mem.index else None, o.mem.disp & 0xffffffff, o.access))
        elif o.type == x86.X86_OP_IMM:
            ops.append(('i', None, None, o.imm & 0xffffffff, 0))
        elif o.type == x86.X86_OP_REG:
            ops.append(('r', i.reg_name(o.reg), None, 0, o.access))
    INS.append((i.address, i.size, i.mnemonic, i.op_str, ops))
STARTS = {a for a, *_ in INS}
FAM = (0x0040e960, 0x00412250)          # the input routine 0x00410100 and its helpers
in_fam = lambda va: FAM[0] <= va < FAM[1]
W = capstone.CS_AC_WRITE
print('  .text 0x%08x + 0x%x, %d instructions (linear sweep, skipdata)' % (TV, len(T), len(INS)))

# ---------------------------------------------------------------- 1. the nine flags
print('=' * 110)
print('1. the nine input flags')
FLAGS = [0x00608e60, 0x00608e88, 0x00608eb0, 0x00608ed8, 0x00608f0c, 0x00608f40, 0x00608f6c, 0x00608f98, 0x00608fd0]
REC = {0x00608e50: 'yaw', 0x00608e78: 'pitch', 0x00608ea0: 'roll', 0x00608ec8: 'turret yaw',
       0x00608efc: 'turret pitch', 0x00608f30: 'strafe x', 0x00608f5c: 'strafe y', 0x00608f88: 'speed',
       0x00608fc0: 'look'}
check(sorted(r + 0x10 for r in REC) == FLAGS, 'each flag is record + 0x10 of one of nine records')
absw, absr = {}, {}
for va, n, mn, s, ops in INS:
    for k, b, x, v, acc in ops:
        if k == 'm' and b is None and x is None and v in FLAGS:
            (absw if acc & W else absr).setdefault(v, []).append(va)
for f in FLAGS:
    print('   %08x (%-12s) abs writers %s; abs readers %s' % (f, REC[f - 0x10], ' '.join('%08x' % a for a in absw.get(f, [])),
                                                         ' '.join('%08x' % a for a in absr.get(f, []))))
CLEAR = [0x00410b8b + 5 * k for k in range(9)]
EXPW = {0x00608e60: [0x00410b8b, 0x00411036, 0x004111a2, 0x00411329, 0x00411496, 0x0041173f, 0x00411783],
        0x00608f40: [0x00410ba4, 0x00411f0c], 0x00608f6c: [0x00410ba9, 0x00411e6e]}
check(all(absw[f] == EXPW.get(f, [CLEAR[k]]) for k, f in enumerate(FLAGS)),
      'absolute writers: the clear 0x00410b8b..0x00410bb3, e60 x6 (0x00411036/0x004111a2/0x00411329/0x00411496 '
      'digital-axis yaw, 0x0041173f/0x00411783 yaw keys), f40 0x00411f0c, f6c 0x00411e6e; nothing else in .text')
check(all(absr.get(f) == [0x004120c5 + 8 * k] for k, f in enumerate(FLAGS)),
      'absolute readers: only the nine cmps 0x004120c5..0x00412105 of the stop block')
# record base immediates, whole .text, and data dwords
imm = []
for va, n, mn, s, ops in INS:
    for k, b, x, v, acc in ops:
        if k == 'i' and 0x00608e50 <= v < 0x00609020: imm.append((va, v))
print('   record-range immediates: ' + ' '.join('%08x=%x' % p for p in imm))
check(all(in_fam(va) and v in REC for va, v in imm) and len(imm) == 30,
      '30 immediates in [0x00608e50,0x00609020), all record bases, all inside the input family')
data_refs = []
for vs, sva, rs, ra in secs:
    if sva == 0x1000: continue
    blob = d[ra:ra + rs]
    for i in range(0, len(blob) - 3):
        v = struct.unpack_from('<I', blob, i)[0]
        if 0x00608e50 <= v < 0x00609020: data_refs.append(0x400000 + sva + i)
check(not data_refs, 'no dword outside .text points into the records (%d found)' % len(data_refs))
# helpers are called only from inside the family
HELP = [0x0040e960, 0x0040e9a0, 0x0040ea30, 0x0040eaa0, 0x0040eb60, 0x0040ec50, 0x0040ecc0, 0x0040edc0, 0x0040eeb0,
        0x0040ef00, 0x0040efd0, 0x0040f100, 0x0040f130, 0x0040f1e0, 0x0040f2f0, 0x0040f350, 0x0040f4f0, 0x0040f580,
        0x0040f690, 0x0040f720, 0x0040f7d0, 0x0040f810, 0x0040f850, 0x0040fc20, 0x0040fe40, 0x0040fec0]
callers = {}
for va, n, mn, s, ops in INS:
    if mn in ('call', 'jmp') and ops and ops[0][0] == 'i' and ops[0][3] in HELP:
        callers.setdefault(ops[0][3], []).append(va)
check(all(callers.get(h) for h in HELP) and all(in_fam(c) for h in HELP for c in callers[h]),
      '%d input helpers, every direct caller inside [0x%08x,0x%08x)' % (len(HELP), FAM[0], FAM[1]))
hset = set(HELP)
hdw = [i for i in range(len(d) - 3) if struct.unpack_from('<I', d, i)[0] in hset]
check(not hdw, 'no dword anywhere in the file holds a helper entry address (no function-pointer use; %d found)' % len(hdw))
# pointer-based flag writes inside the helpers, with the record each receives
PTR = [  # (site, bytes, record, how the record arrives)
    (0x0040ec28, 'c7 47 10 01 00 00 00', 0x00608e50, 0x00411db0, 'bf 50 8e 60 00 e8 a6 cd ff ff', 'yaw controller 0x0040eb60, EDI'),
    (0x0040ee88, 'c7 47 10 01 00 00 00', 0x00608e78, 0x00411dba, 'bf 78 8e 60 00 e8 fc cf ff ff', 'pitch controller 0x0040edc0, EDI'),
    (0x0040f0ed, 'c7 46 10 01 00 00 00', 0x00608ea0, 0x00411dc6, 'be a0 8e 60 00 e8 00 d2 ff ff', 'roll controller 0x0040efd0, ESI'),
    (0x0040f2b8, 'c7 46 10 01 00 00 00', 0x00608ec8, 0x00412048, 'b8 c8 8e 60 00 e8 8e d1 ff ff', 'turret controller 0x0040f1e0, EAX->ESI'),
    (0x0040f2b8, 'c7 46 10 01 00 00 00', 0x00608efc, 0x0041205f, 'b8 fc 8e 60 00 e8 77 d1 ff ff', 'turret controller 0x0040f1e0, EAX->ESI'),
    (0x0040f3e7, '89 4d 10', 0x00608f88, 0x00411284, '68 88 8f 60 00', 'speed key step 0x0040f350, [esp+..]->EBP, ECX=1'),
    (0x0040f568, '89 45 10', 0x00608f88, 0x00411d39, '68 88 8f 60 00', 'boost 0x0040f4f0, EBP, EAX=1'),
    (0x0040f63b, 'c7 42 10 01 00 00 00', 0x00608f88, 0x0040fd40, '68 88 8f 60 00 e8 36 f8 ff ff', 'analog speed axis 0x0040f580'),
    (0x0040f712, 'c7 40 10 01 00 00 00', 0x00608f88, 0x0040fe8c, '68 88 8f 60 00', 'throttle 0x0040f690'),
    (0x0040f7ba, 'c7 45 10 01 00 00 00', 0x00608f88, 0x00411aa9, '68 88 8f 60 00', 'throttle 0x0040f720'),
    (0x0040f9ba, 'c7 46 10 01 00 00 00', 0x00608fc0, 0x004120bb, 'be c0 8f 60 00 e8 8b d7 ff ff', 'look 0x0040f850, ESI'),
]
for site, hx, rec, cs, chx, what in PTR:
    ok = rd(site, len(bytes.fromhex(hx))) == bytes.fromhex(hx) and rd(cs, len(bytes.fromhex(chx))) == bytes.fromhex(chx)
    check(ok, '%08x writes %s flag %08x (%s; record from %08x)' % (site, REC[rec], rec + 0x10, what, cs))
check(rd(0x0040f1e9, 2) == bytes.fromhex('8b f0'), '0x0040f1e0: mov esi,eax (record in ESI for 0x0040f2b8)')
# every [reg+0x10] read in the family: none has a record base
reads10 = []
for va, n, mn, s, ops in INS:
    if not in_fam(va): continue
    for k, b, x, v, acc in ops:
        if k == 'm' and b not in (None, 'esp') and x is None and v == 0x10 and not (acc & W) and mn != 'lea':
            reads10.append((va, b))
print('   [reg+0x10] reads in the family: ' + ' '.join('%08x[%s]' % p for p in reads10))
check([va for va, _ in reads10] == [0x0040f39e, 0x0040f4f2, 0x0040f5cc, 0x0040f860, 0x0041022c, 0x00410cff, 0x00411c8d,
                                    0x00411ce0],
      'eight [reg+0x10] reads in the family: ship speed obj+0x10 (0x0040f39e/0x0040f4f2/0x0040f5cc), the cockpit '
      'registry [*0x00608504+0x10] (0x0040f860/0x0041022c/0x00410cff/0x00411c8d), the target speed (0x00411ce0); '
      'none reads a flag')
for va, hx, what in [(0x0040f39e, '8b 47 10', 'f350 EDI = ship'), (0x0040f4f2, '8b 5f 10', 'f4f0 EDI = ship'),
                     (0x0040f5cc, '8b 46 10', 'f580 ESI = ship'), (0x0040f85b, 'a1 04 85 60 00 8b 50 10', 'f850 registry'),
                     (0x00410226, '8b 0d 04 85 60 00 8b 79 10', 'registry'), (0x00410ce2, 'a1 04 85 60 00', 'registry ...'),
                     (0x00410cff, '8b 50 10', '... +0x10'), (0x00411c80, 'a1 04 85 60 00 85 c0 0f 84 cf 00 00 00 8b 50 10',
                                                            'registry (match speed)'),
                     (0x00411cd2, '8b 80 e0 01 00 00 85 c0 0f 84 7c 00 00 00 8b 70 10', 'target = [cockpit+0x1e0]; its speed +0x10')]:
    check(rd(va, len(bytes.fromhex(hx))) == bytes.fromhex(hx), '%08x %s' % (va, what))
# autopilot-cancel flags (+0x14), the parallel block
check(rd(0x00412136, 13) == bytes.fromhex('f7 47 40 00 00 08 00 0f 84 c2 00 00 00') and
      rd(0x00412143, 6) == bytes.fromhex('39 1d 64 8e 60 00') and rd(0x00412196, 5) == bytes.fromhex('68 0c 5e 55 00')
      and cstr(0x00555e0c) == 'SetAutopilot',
      '0x00412136: under autopilot (0x80000) the record+0x14 flags call SetAutopilot(1, 0): a separate block')

# ---------------------------------------------------------------- input sources
print('=' * 110)
print('1b. input sources')
SRC = [
    (0x00410200, '80 83 68 fc 57 00 01', 'key/button/hat press -> action counter 0x0057fc68[TranslateKey result] += 1'),
    (0x004108f8, 'bf 40 01 00 00', 'hat 0 deg -> key 0x140'), (0x00410912, 'bf 47 01 00 00', 'hat -> key 0x147'),
    (0x0041170e, '80 3d 77 fe 57 00 00', 'action 527 (Yaw Right) counter 0x0057fe77'),
    (0x0041175b, '80 3d 76 fe 57 00 00', 'action 526 (Yaw Left) counter 0x0057fe76'),
    (0x0040fb2b, 'b8 0b 00 00 00 ba 03 00 00 00 a3 40 8e 60 00', 'default mouse X axis type 0xb (InputMouseDelta)'),
    (0x0040fb3f, 'b8 0c 00 00 00', 'default mouse Y axis type 0xc ...'), (0x0040fbb6, 'a3 44 8e 60 00', '... stored to 0x00608e44'),
    (0x0040fef8, 'be 50 8e 60 00', 'mouse type 1 -> yaw record'), (0x0040fefd, 'e8 9e eb ff ff', '... 0x0040eaa0'),
    (0x0040ff61, 'ba c0 8f 60 00 e8 65 f8 ff ff', 'mouse type 9 -> look record via 0x0040f7d0'),
    (0x0040ff82, 'ba c0 8f 60 00 e8 84 f8 ff ff', 'mouse type 10 -> look record via 0x0040f810'),
    (0x0040eaa5, '8b 80 14 07 00 00 83 f8 0a', 'mouse delta: game step cfg+0x714, 10 ms window'),
    (0x0040eb1e, 'b8 00 80 00 00 8b 54 24 0c 8b 4c 24 08 f7 ea f7 f9 f7 d8', 'mouse yaw rate = -delta*0x8000/ms'),
    (0x0040e961, 'a1 9c 8c 60 00', 'stick deflection: sensitivity % at 0x00608c9c'),
    (0x0040f851, '83 7e 48 00 0f 84 84 00 00 00', 'look: direct-angle mode (+0x48) has no flag write'),
    (0x0040f9a4, '68 00 5e 55 00 68 96 00 00 00 6a 00 6a 00 e8 09 fb 08 00', 'look: InputAction(0x2cd..0x2d4) ...'),
    (0x00412218, '8b 0d 14 90 60 00 51 50 6a 02 6a 01 68 fc 56 55 00', 'mouse delta -> InputMouseDelta(dx, dy)'),
]
for va, hx, what in SRC:
    check(rd(va, len(bytes.fromhex(hx))) == bytes.fromhex(hx), '%08x %s' % (va, what))
check(cstr(0x00555e00) == 'InputAction' and cstr(0x005556fc) == 'InputMouseDelta' and cstr(0x00555e1c) == 'StopFastForward',
      'strings InputAction / InputMouseDelta / StopFastForward')
check(0x0057fe76 - 0x0057fc68 == 526 and 0x0057fe77 - 0x0057fc68 == 527, 'counter address = 0x0057fc68 + action')


def pck(cat, path):
    for e in read_catalogue(cat):
        if e['path'] == path:
            with open(cat.with_suffix('.dat'), 'rb') as f:
                f.seek(e['offset']); return gzip.decompress(bytes(b ^ 0x33 for b in f.read(e['size']))).decode('utf-8', 'replace')


t = pck(X3 / 'addon/04.cat', 'addon/t/0001-L044.pck')
a = t.find('<page id="301903"'); page = t[a:t.find('</page>', a)]
name = lambda i: (re.search(r'<t id="%d">([^<]*)</t>' % (10000 + i), page) or [None, None])[1]
ACT = {524: 'Pitch Up', 525: 'Pitch Down', 526: 'Yaw Left', 527: 'Yaw Right', 528: 'Roll left', 529: 'Roll right',
       531: 'Accelerate', 532: 'Decelerate', 534: 'Boost extension', 540: 'Strafe left', 541: 'Strafe right',
       542: 'Strafe up', 543: 'Strafe down', 544: 'Brake to Stop', 717: 'View from west', 724: 'View from southwest'}
print('   page 301903: ' + '; '.join('%d %r' % (k, name(k)) for k in sorted(ACT)))
check(all(name(k) == v for k, v in ACT.items()), 'control names 10000+action for the counters 0x0057fe74.. and the look actions')

# ---------------------------------------------------------------- 2. the stop block and the patch
print('=' * 110)
print('2. stop block and patch')
check(rd(0x004120c5, 72) == b''.join(bytes.fromhex('39 1d') + struct.pack('<I', f) + (bytes.fromhex('75') + bytes([0x40 - 8 * k])
                                                                                    if k < 8 else bytes.fromhex('74 29')) for k, f in enumerate(FLAGS)),
      '0x004120c5..0x0041210c: cmp [flag],ebx; jne 0x0041210d (x8); last je 0x00412136')
check(rd(0x0041210d, 18) == bytes.fromhex('8b 15 34 6f 60 00 81 ba cc 00 00 00 00 00 01 00 74 17'),
      '0x0041210d mov edx,[cfg]; 0x00412113 cmp [edx+0xcc],0x10000; 0x0041211d je 0x00412136')
check(rd(0x0041211f, 23) == bytes.fromhex('53 6a 01 68 1c 5e 55 00 68 96 00 00 00 53 53 e8 8d d3 08 00 83 c4 18'),
      '0x0041211f..0x00412135: 0x0049f4c0(0, 0, 0x96, "StopFastForward", 1, 0); add esp,0x18')
check(rd(0x004120a5, 2) == bytes.fromhex('33 db'), 'EBX = 0 from 0x004120a5 (the cmps compare against 0)')
check(all(x in STARTS for x in (0x0041210d, 0x00412113, 0x0041211d, 0x0041211f, 0x00412136)) and 0x0041211e not in STARTS,
      'instruction starts 0x0041210d/0x00412113/0x0041211d/0x0041211f/0x00412136; 0x0041211e is the rel8')
# every branch into [0x0041210d, 0x00412136]
into = []
for va, n, mn, s, ops in INS:
    if mn.startswith('j') or mn == 'call':
        if ops and ops[0][0] == 'i' and 0x0041210d <= ops[0][3] <= 0x00412136: into.append((va, ops[0][3]))
print('   branches into 0x0041210d..0x00412136: ' + ' '.join('%08x->%08x' % p for p in into))
check(sorted(into) == sorted([(0x004120cb + 8 * k, 0x0041210d) for k in range(8)] + [(0x0041210b, 0x00412136),
                                                                                       (0x0041211d, 0x00412136)]),
      'only the eight jne -> 0x0041210d and the two je -> 0x00412136; nothing enters 0x0041210e..0x00412135')
dw = [TV + i for i in range(len(T) - 3) if 0x0041210d <= struct.unpack_from('<I', T, i)[0] <= 0x00412136]
check(not dw, 'no .text dword holds an address in 0x0041210d..0x00412136 (no jump-table entry)')
check((0x0041211d & 7) + 1 <= 8 and (0x0041211d & ~7) == 0x00412118,
      '0x0041211d lies in the aligned qword 0x00412118 (engine_patch::write_code takes the lock cmpxchg8b path)')
pat = bytearray(d); pat[off(0x0041211d)] = 0xeb
md2 = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
a0, a1 = 0x004120bb, 0x00412136
orig = [(i.address, i.size) for i in md2.disasm(d[off(a0):off(a1)], a0)]
new = [(i.address, i.size, i.mnemonic, i.op_str) for i in md2.disasm(bytes(pat[off(a0):off(a1)]), a0)]
check([x[:2] for x in new] == orig and [x for x in new if x[0] == 0x0041211d][0][2:] == ('jmp', '0x412136'),
      'patched image: 0x0041211d decodes as jmp 0x00412136, every instruction start 0x004120bb..0x00412135 unchanged')
check(not (0x0041211f <= 0x0041211d < 0x0041211f + 23), 'patch byte outside the seta_restore stop_call window 0x0041211f+23')
tail = [(va, mn, s) for va, n, mn, s, ops in INS if 0x00412136 <= va < 0x00412250]
check(not [x for x in tail if re.search(r'\b(edx|dx|dl|dh)\b', x[2])] and tail[0][1] == 'test',
      'EDX is not referenced in 0x00412136..0x0041224f and 0x00412136 rewrites EFLAGS: both dead at the join')
check(rd(0x00412246, 10) == bytes.fromhex('b8 01 00 00 00 5b 8b e5 5d c3'), 'routine ends mov eax,1; pop ebx; leave-style epilogue; ret')

# ---------------------------------------------------------------- 3. controllers, integrator, strafe, speed
print('=' * 110)
print('3. rotation, strafe, speed')
CTRL = [  # (entry, autopilot test, step span, threshold, flag, write)
    ('yaw', 0x0040eb60, '51 85 f6 74 0d f7 46 40 00 00 08 00', 0x0040ebde, 'a1 34 6f 60 00 da 88 14 07 00 00 8b 4f 24',
     0x0040ec18, 0x0040ec2f, 'd9 07 d8 0d 58 55 56 00 e8 94 c9 11 00 89 46 24'),
    ('pitch', 0x0040edc0, '51 85 f6 74 0d f7 46 40 00 00 08 00', 0x0040ee3e, 'a1 34 6f 60 00 da 88 14 07 00 00 8b 4f 24',
     0x0040ee78, 0x0040ee8f, 'd9 07 d8 0d 58 55 56 00 e8 34 c7 11 00 89 46 28'),
    ('roll', 0x0040efd0, '51 85 ff 74 0d f7 47 40 00 00 08 00', 0x0040f04d, 'a1 34 6f 60 00 da 88 14 07 00 00 8b 4e 24',
     None, None, None),
]
for nm, e, hx, sp, shx, thr, wr, whx in CTRL:
    check(rd(e, 12) == bytes.fromhex(hx), '%s controller %08x: returns 0 at once under autopilot (obj+0x40 & 0x80000)' % (nm, e))
    check(rd(sp, 14) == bytes.fromhex(shx), '%s %08x: mov eax,[cfg]; %08x fimul [eax+0x714] (game ms); %08x mov ecx,[rec+0x24] (target)'
          % (nm, sp, sp + 5, sp + 11))
    if thr: check(rd(thr, 6) == bytes.fromhex('d8 1d a0 56 56 00'), '%s %08x fcomp [0x005656a0] (change threshold)' % (nm, thr))
    if wr: check(rd(wr, 16) == bytes.fromhex(whx), '%s %08x: rate*65536 -> obj+0x%02x' % (nm, wr, 0x24 if nm == 'yaw' else 0x28))
check(rd(0x0040eb72, 18) == bytes.fromhex('8b c6 e8 e7 21 04 00 8b 4f 24 3b c8 7f 06 f7 d8 3b c8') and
      rd(0x0040eb86, 3) == bytes.fromhex('89 47 24'), 'yaw: target clamped to +-max yaw rate 0x00450d60 (0x0040eb79..0x0040eb86)')
check(rd(0x0040eb89, 5) == bytes.fromhex('66 83 7e 48 07') and rd(0x0040ebb4, 5) == bytes.fromhex('ba 99 79 00 00') and
      rd(0x0040ebd0, 6) == bytes.fromhex('d8 0d e0 54 56 00') and rd(0x0040ebd8, 6) == bytes.fromhex('d9 05 a4 56 56 00'),
      'yaw accel: ships (class 7) (yaw+pitch+roll max) * 0x7999/65536 * 2^-16, other classes [0x005656a4]')
check(rd(0x0040ebef, 6) == bytes.fromhex('dc 0d 00 55 56 00') and rd(0x0040ec05, 5) == bytes.fromhex('e8 96 fd ff ff'),
      'yaw step = accel * cfg+0x714 * [0x00565500]; slew 0x0040e9a0 (0x0040ec05)')
check(rd(0x0040f052, 6) == bytes.fromhex('da 88 14 07 00 00') and rd(0x0040f0ed, 7) == bytes.fromhex('c7 46 10 01 00 00 00'),
      'roll 0x0040f052 fimul game ms; 0x0040f0ed flag')
CONST = {0x005654e0: 2 ** -16, 0x005656a0: 0.0049896240234375, 0x005656a4: 0.79998779296875, 0x00565558: 65536.0,
         0x00565650: 20000.0}
for va, v in CONST.items(): check(f32(va) == v, 'float %08x = %r' % (va, v))
check(f64(0x00565500) == 0.001, 'double 0x00565500 = 0.001')
check(rd(0x0040e9a0, 12) == bytes.fromhex('db 44 24 04 d8 0d e0 54 56 00 d8 11'), 'slew 0x0040e9a0: target int * 2^-16 vs current float')
# hook spans of the controllers: no branch into their interior
for nm, e, hx, sp, *_ in CTRL:
    bad = [(va, o[0][3]) for va, n, mn, s, o in INS if (mn.startswith('j') or mn == 'call') and o and o[0][0] == 'i'
           and sp < o[0][3] < sp + 14]
    joins = [va for va, n, mn, s, o in INS if mn.startswith('j') and o and o[0][0] == 'i' and o[0][3] == sp]
    check(not bad and all(x in STARTS for x in (sp, sp + 5, sp + 11, sp + 14)),
          '%s span %08x..%08x: three whole instructions, entered only at its first byte (join from %s)' % (
              nm, sp, sp + 13, ' '.join('%08x' % j for j in joins)))
e9 = [(va, mn, s) for va, n, mn, s, o in INS if 0x0040e9a0 <= va < 0x0040ea30]
check(not [x for x in e9 if re.search(r'\b(edx|dx|dl)\b', x[2])] and
      all(x[1] == 'fnstsw' for x in e9 if re.search(r'\b(eax|ax)\b', x[2]) and x[1] != 'fnstsw') ,
      '0x0040e9a0 never reads EDX; its only EAX use is fnstsw (write): EAX/EDX dead after each span')
# integrator, manual rotation
INT = [(0x004597dd, '2b b3 a8 00 00 00 33 d2 81 fe e8 03 00 00', 'dt = now - obj+0xa8 (game ms), 1000 cap'),
       (0x00459926, 'f7 c1 00 00 08 00 0f 84 a9 03 00 00', 'autopilot clear -> manual branch 0x00459cdb'),
       (0x00459cdb, '8b 53 28 89 54 24 14 8b 44 24 14 8b 54 24 24 b9 e8 03 00 00 f7 ea f7 f9', 'dpitch = obj+0x28 * dt / 1000'),
       (0x00459cf7, '8b 43 24 89 44 24 14 8b 44 24 14 8b 54 24 24 b9 e8 03 00 00 f7 ea f7 f9', 'dyaw = obj+0x24 * dt / 1000'),
       (0x00459ece, '8b 53 2c 89 54 24 14 8b 44 24 14 8b 54 24 24 b9 e8 03 00 00 f7 ea f7 f9', 'droll = obj+0x2c * dt / 1000'),
       (0x00459d2c, '8b 15 0c 85 60 00 8b 42 3c a8 02', 'player auto-level gate: universe flag 2'),
       (0x00459dfd, 'e8 1e 0f fe ff', 'auto-level roll through 0x0043ad20 (game dt)'),
       (0x0045a74e, 'b9 d0 07 00 00 2b 4c 24 24', 'slip *= (2000 - dt) / 2000')]
for va, hx, what in INT:
    check(rd(va, len(bytes.fromhex(hx))) == bytes.fromhex(hx), '%08x %s' % (va, what))
# strafe
STR = [(0x00411dd0, 'd9 05 50 56 56 00 83 3d 84 8f 60 00 00', 'strafe: c = [0x00565650] (20000); max y 0x00608f84'),
       (0x00411ddd, 'dd 05 00 55 56 00', '0.001'), (0x00411e0c, 'db 86 14 07 00 00', 'strafe y step: game ms'),
       (0x00411e18, 'db 05 80 8f 60 00 d8 f2', 'strafe y command 0x00608f80 / max'),
       (0x00411e6e, 'c7 05 6c 8f 60 00 01 00 00 00', 'flag f6c whenever command and max are non-zero'),
       (0x00411eb2, 'db 86 14 07 00 00', 'strafe x step: game ms'),
       (0x00411ec2, 'db 05 54 8f 60 00 d8 f3', 'strafe x command 0x00608f54 / max'),
       (0x00411f0c, 'c7 05 40 8f 60 00 01 00 00 00', 'flag f40'),
       (0x00411ff6, '8b 75 08 83 c6 60 03 d8 8b 06 03 c1', 'increments added to the slip velocity obj+0x60..0x68')]
for va, hx, what in STR:
    check(rd(va, len(bytes.fromhex(hx))) == bytes.fromhex(hx), '%08x %s' % (va, what))
# speed
SPD = [(0x0040f35f, '8b b0 14 07 00 00 8b c7 e8 14 16 04 00', 'speed key: game ms, max speed 0x00450980'),
       (0x0040f36c, '83 fe 64', 'step ms capped at 100'), (0x0040f38b, 'b9 ee 02 00 00 f7 ea f7 f9', 'step = max * ms / 750 ...'),
       (0x0040f3db, '3d fa 00 00 00 b9 01 00 00 00 7e 03 89 4d 10', 'flag f98 when the step exceeds 250'),
       (0x0040f521, '8b 88 14 07 00 00', 'boost: game ms'), (0x0040f58f, '8b b8 14 07 00 00', 'analog speed axis: game ms')]
for va, hx, what in SPD:
    check(rd(va, len(bytes.fromhex(hx))) == bytes.fromhex(hx), '%08x %s' % (va, what))

# ---------------------------------------------------------------- additional sites quoted in the note
print('=' * 110)
print('extra sites')
EXTRA = [(0x00410b81, '85 db 0f 84 2a 16 00 00', 'records cleared/re-seeded only when the controlled object EBX != 0'),
         (0x00410be5, 'db 43 24 d9 05 e0 54 56 00', 're-seed: yaw current = obj+0x24 * 2^-16'),
         (0x00410bfa, 'a3 74 8e 60 00', 'yaw command 0x00608e74 = 0 each frame'),
         (0x00410290, '83 b9 d8 01 00 00 00', 'turret view: cockpit entry +0x1d8 > 0'),
         (0x0040f024, 'ba 00 40 00 00', 'roll accel factor 0x4000 (0.25)'),
         (0x0040f2a4, 'f7 ea f7 f9 01 07 3d 1e 05 00 00 7f 07 3d e2 fa ff ff', 'turret: step rate*ms/1000 beyond +-0x51e -> flag'),
         (0x0040f7d8, '83 f9 0f', 'mouse look x: |delta| >= 15'), (0x0040f818, '83 f9 0c', 'mouse look y: |delta| >= 12'),
         (0x0041222e, '56 56 e8 8b d2 08 00', 'InputMouseDelta call 0x00412230 -> 0x0049f4c0'),
         (0x0040eb31, '3d 00 00 01 00 7e 0c', 'mouse rate clamp 0x10000')]
for va, hx, what in EXTRA:
    check(rd(va, len(bytes.fromhex(hx))) == bytes.fromhex(hx), '%08x %s' % (va, what))
EXTRA2 = [(0x0040ec02, '51 8b cf e8 96 fd ff ff', 'yaw: push ecx (target); this = record; call 0x0040e9a0'),
          (0x00411f16, 'e8 b5 96 11 00', 'strafe x ftol'),
          (0x00412019, '89 4e 04 89 56 08 89 46 0c', 'slip obj+0x64/+0x68 stored (and +0x6c), ends 0x0041201f'),
          (0x004121aa, '81 67 40 ff ff f7 ff eb 52', 'autopilot cancel clears 0x80000; block ends 0x004121b1'),
          (0x00459e20, '8b 44 24 18 89 43 2c e9 14 01 00 00', 'auto-level roll rate -> obj+0x2c; branch ends 0x00459e27')]
for va, hx, what in EXTRA2:
    check(rd(va, len(bytes.fromhex(hx))) == bytes.fromhex(hx), '%08x %s' % (va, what))
refs88 = [va for va, n, mn, s_, ops in INS if any(k == 'm' and b is None and v == 0x0057fe88 for k, b, x, v, a_ in ops)]
check(refs88 and all(in_fam(v) for v in refs88), 'action 544 counter 0x0057fe88 read inside the family (%s)' % ' '.join('%08x' % v for v in refs88))
dc = {}
for va, n, mn, s_, ops in INS:
    if mn == 'call' and ops and ops[0][0] == 'i' and ops[0][3] in (0x0040e8c0, 0x00450c10, 0x004d25b0):
        dc.setdefault(ops[0][3], []).append(va)
check(all(c in callers.get(0x0040fec0, []) or in_fam(c) for c in dc[0x0040e8c0]) and
      any(in_fam(c) for c in dc[0x00450c10]) and any(in_fam(c) for c in dc[0x004d25b0]),
      'the family calls 0x0040e8c0 (cursor offset), 0x00450c10 (strafe max), 0x004d25b0 (head tracker): %s' % (
          {'%08x' % k: ['%08x' % c for c in v if in_fam(c)] for k, v in dc.items()}))
c = rd(0x0040747e, 5)
check(c[0] == 0xe8 and 0x0040747e + 5 + struct.unpack('<i', c[1:])[0] == 0x004123a0, '0x0040747e (X2 dispatcher) calls the axis-type setter 0x004123a0')
cs = {}
for va, n, mn, s, ops in INS:
    for k, b, x, v, a_ in ops:
        if k == 'm' and b is None and x is None and v == 0x00607c64: cs.setdefault('W' if a_ & W else 'R', []).append(va)
check(cs == {'W': [0x004074d6], 'R': [0x0042ab24]}, 'cursor-steering state 0x00607c64: one writer 0x004074d6, one reader 0x0042ab24')
p1 = [(va, s) for va, n, mn, s, ops in INS if 0x00412136 <= va <= 0x004121a2]
check(not [x for x in p1 if re.search(r'\b(eax|ax|al|ah|ecx|cx|cl|ch)\b', x[1])] and rd(0x004121a2, 1) == b'\xe8',
      'autopilot path 0x00412136..0x004121a2 references neither EAX nor ECX before its call')
p2 = [(va, s) for va, n, mn, s, ops in INS if 0x00412205 <= va < 0x00412218]
check(rd(0x00412205, 5) == bytes.fromhex('a1 10 90 60 00') and rd(0x00412218, 6) == bytes.fromhex('8b 0d 14 90 60 00') and
      not [x for x in p2 if re.search(r'\b(ecx|cx|cl|ch)\b', x[1])],
      'join path: EAX written at 0x00412205, ECX written at 0x00412218 before any read')

# ---------------------------------------------------------------- 4/5. warp readers
print('=' * 110)
print('4/5. readers of the game step, the wall clock and the warp factor')
acc = {0x714: [], 0x720: [], 0x71c: [], 0xcc: [], 0xd0: []}
for va, n, mn, s, ops in INS:
    if not in_fam(va): continue
    for k, b, x, v, a_ in ops:
        if k == 'm' and b not in (None, 'esp', 'ebp') and v in acc: acc[v].append(va)
for k, v in acc.items(): print('   family [reg+0x%x]: %s' % (k, ' '.join('%08x' % a for a in v)))
check(acc[0x714] == [0x0040eaa5, 0x0040ebe3, 0x0040ecc5, 0x0040ee43, 0x0040ef05, 0x0040f052, 0x0040f136, 0x0040f217,
                     0x0040f35f, 0x0040f521, 0x0040f58f, 0x00411e0c, 0x00411eb2],
      '13 game-step reads in the family (mouse x4, ramps x3, turret, speed key, boost, speed axis, strafe x2)')
check(acc[0x720] == [0x0040f427, 0x0040f4cc, 0x00410a8a, 0x00410ac9] and not acc[0x71c],
      'wall clock +0x720: speed-key 1000 ms window x2, key tap/hold x2; no +0x71c read')
check(acc[0xcc] == [0x00412113] and not acc[0xd0], 'the family reads cfg+0xcc once (0x00412113) and never +0xd0')
WARP = [(0x0044d67d, '8b 0d 34 6f 60 00 8b 81 cc 00 00 00 2d 00 00 01 00 99', 'margin * (1 + (warp-1)/4) (near steering dispatcher)'),
        (0x0045431d, '8b 15 34 6f 60 00 8b 82 cc 00 00 00 03 8b a4 00 00 00 2d 00 00 01 00', 'docking margin, same law'),
        (0x00496f85, '8b 35 34 6f 60 00 8b 86 cc 00 00 00 db 86 14 07 00 00 33 db 3d 00 00 01 00', 'auto-detail: +0x714 / warp')]
for va, hx, what in WARP:
    check(rd(va, len(bytes.fromhex(hx))) == bytes.fromhex(hx), '%08x %s' % (va, what))

print('\nRESULT %s (%d failures)' % ('PASS' if not FAIL else 'FAIL', len(FAIL)))
for f in FAIL: print('  FAIL: ' + f)
