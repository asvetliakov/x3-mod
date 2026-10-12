#!/usr/bin/env python3
"""Build the gate-queue patch options in memory for stock AP and Mayhem 4, validate them, print the byte changes.

Options (docs/reverse-engineering/gate-queue-script.md section 4):
  A  constants only: through-slot cooldown and warp-in stamp (pushb 60 -> N), SA_Idle wander and slot polls
     (pushw 1000 / 10000 -> new values); A5 (optional) always grant the through slot; A6 (optional) same-direction
     exemption also out of sector.
  B  clearance test: ReleaseSlot keeps the holder (m15) and only stamps m16; __requestWarpThroughSlot is rewritten in
     place (same entry, same length, padded) so a released holder frees the slot when it is gone, travels in the same
     direction, has left the exit gate's sector or is farther than D from the exit gate, or when m16 (now + 60) passes.
  C  remove the stationary SA_RotateToAngles step: the 27 bytes that push the angles and call it become
     "push1; jmp <continuation>" plus padding, so the code after the call sees result 1 as before.

Every option keeps the file length, the build id and every instruction start outside the patched windows, so saved
task IPs (raw CODE offsets) stay valid. Checks per option and build: the whole CODE still decodes with the widths of
0x0049e1a0 to its exact end, every method entry is an instruction start, each patched method tracks with zero stack
depth conflicts and every jump stays inside it, the frame reservation covers the new maximum depth, and the suspend
points of __EnterHyperspace keep their IPs and depths. Nothing is written to disk; only offsets and the mod-authored
replacement bytes are printed.

    python3 -I verification/results/ship-navigation-ai/gate-queue/gate_queue_patches.py [X3 dir] > gate_queue_patches.txt
"""
import struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from kc_obj import Obj, load_member, BIN
from gate_sites import find_sites, member_map

X3 = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3')
BUILDS = [('stock AP', X3 / 'addon/04.cat'), ('Mayhem 4', X3 / 'addon/07.cat')]
# Parameters of the example patches (seconds of class-402 age; milliseconds for the waits; engine units for D)
COOLDOWN_S, STAMP_S = 5, 5
IDLE_MIN, IDLE_RAND = 250, 1000          # SA_Idle(m3, IDLE_MIN + SE_Random(IDLE_RAND))
POLL_MIN, POLL_RAND = 250, 1000          # TI_WaitRandomForInterrupt(POLL_MIN, POLL_RAND, this)
CLEAR_D = 1500000                        # 3 km at 500 units/m [i]
STAMP_B = 60                             # B keeps the stock warp-in stamp
BLOCKING = {'SA_Idle', 'TI_WaitRandomForInterrupt', 'SA_RotateToAngles', 'SA_FlyObjectBeforeContainer',
            'SA_FlyObjectBeforeContainerPos', 'SA_FlyObjectIntoContainer', 'SA_WarpObjectAway',
            'SA_AvoidObjectCollision'}


def method_range(o, cid, nm):
    e = o.find(cid, nm)[0]; ins = list(o.insns(e)); return e, ins[-1] + o.width(ins[-1]), ins


def name_off(o, name):
    """STRG offset of a method name from any CLAS row (0x85 operands are STRG offsets resolved at run time)."""
    raw = o.s; i = raw.find(b'\0' + name.encode() + b'\0')
    assert i >= 0, name
    return i + 1


def native_any(o, name):
    for pc in o.all_insns():
        if o.code[pc] == 0x82 and o.operand(pc) == name: return struct.unpack_from('>I', o.code, pc + 1)[0]
    raise KeyError(name)


def native_off(o, e, end, name):  # first native call of that name inside [e, end)
    for pc in o.insns(e):
        if o.code[pc] == 0x82 and o.operand(pc) == name: return struct.unpack_from('>I', o.code, pc + 1)[0]
    raise KeyError(name)


class Asm:
    """Tiny assembler: tracks the stack depth so 'ldloc' can take absolute slots (k >= 0 locals, k < 0 arguments)."""
    def __init__(self, base): self.base, self.out, self.d, self.lab, self.fix, self.dmax, self.at = base, bytearray(), 0, {}, [], 0, {}

    def emit(self, b, dd):
        self.out += b; self.d += dd; self.dmax = max(self.dmax, self.d)

    def label(self, n, depth=None):
        if depth is not None: self.d = depth
        assert n not in self.lab; self.lab[n] = self.base + len(self.out); self.at[n] = self.d

    def push(self, v):
        if 0 <= v <= 3: self.emit(bytes([1 + v]), 1)
        elif -128 <= v <= 127: self.emit(bytes([5, v & 255]), 1)
        elif -32768 <= v <= 32767: self.emit(b'\x06' + struct.pack('>h', v), 1)
        else: self.emit(b'\x07' + struct.pack('>i', v), 1)

    def ldloc(self, k): self.emit(b'\x0d' + struct.pack('>h', self.d - k), 1)
    def stloc(self, k): self.emit(b'\x14' + struct.pack('>h', self.d - k), 0)
    def ldglob(self, g): self.emit(b'\x0e' + struct.pack('>h', g), 1)
    def ldmem(self, m): self.emit(b'\x0f' + struct.pack('>h', m), 1)
    def stmem(self, m): self.emit(b'\x16' + struct.pack('>h', m), 0)
    def this(self): self.emit(b'\x2e', 1)
    def drop(self, n=1): self.emit(b'\x24', -1) if n == 1 else self.emit(b'\x23' + struct.pack('>h', n), -n)
    def op(self, code): self.emit(bytes([code]), 0 if code in (0x64, 0x65) else -1)
    def native(self, off, argc): self.emit(b'\x82' + struct.pack('>I', off), -argc)        # args, argc pushed
    def callobj(self, off, argc): self.emit(b'\x85' + struct.pack('>I', off), -(argc + 1))  # args, argc, object
    def callself(self, entry, argc): self.emit(b'\x88' + struct.pack('>I', entry), -argc)  # args, argc
    def ret(self): self.emit(b'\x83', 0)

    def jump(self, code, n):
        self.fix.append((len(self.out) + 1, n, self.d - (code != 0x32)))
        self.emit(bytes([code]) + b'\0\0\0\0', 0 if code == 0x32 else -1)

    def done(self):
        for at, n, d in self.fix:
            assert self.at[n] == d, ('depth mismatch at label', n, self.at[n], d)
            struct.pack_into('>I', self.out, at, self.lab[n])
        return bytes(self.out)


def finish(a, e, end):
    body = bytearray(a.done())
    if struct.unpack_from('>h', body, 1)[0] < a.dmax: struct.pack_into('>h', body, 1, a.dmax)
    assert len(body) <= end - e, ('rewrite too long', len(body), end - e)
    used = len(body)
    body += b'\x01' * (end - e - len(body))                              # unreachable 1-byte padding (push0)
    return bytes(body), used


def build_b(o, mem, g):
    """B: in-place rewrites of 2014::__requestWarpThroughSlot(ship, side, gm3) and 2014::ReleaseSlot(ship).

    A released through-slot holder is kept as m16 = {ship, now + 60} (element 0 = ship). Stock code reads that array
    in "m16 <= now" as true (the comparison helpers 0x004a8b40/0x004a8bb0 return false for non-numbers), so a save
    made with B and loaded without it sees a free slot; an int m16 from a stock save is ignored by B.
    """
    H, T, S, A14, CTRL = mem[15], mem[16], mem[17], mem[14], mem[18]
    e, end, ins = method_range(o, 2014, '__requestWarpThroughSlot')
    getage = struct.unpack_from('>I', o.code, next(pc for pc in ins if o.code[pc] == 0x85) + 1)[0]
    exists = native_off(o, e, end, 'SE_ObjectExists')
    release = o.find(2014, 'ReleaseSlot')[0]; destgate = o.find(2014, 'GetDestinationGate')[0]
    getid = name_off(o, 'GetObjectID'); dist = native_any(o, 'SA_Distance'); etype = native_any(o, 'SE_GetExprType')
    isctrl = name_off(o, 'IsWarpController'); relname = name_off(o, 'ReleaseSlot')
    SHIP, SIDE, GM3, NOW = -4, -5, -6, 0
    a = Asm(e)
    a.emit(o.code[e:e + 3], 0)                                           # frame N (raised if needed)
    a.push(0); a.ldglob(g); a.callobj(getage, 0)                         # now = g8.GetAge()
    # stock reset of a held slot: holder gone, or the holder is the requester -> m15 = 0, m16 = 0
    a.ldmem(H); a.jump(0x34, 'check')
    a.ldmem(H); a.push(1); a.native(exists, 1); a.op(0x64); a.jump(0x33, 'reset')
    a.ldloc(SHIP); a.ldmem(H); a.op(0x5b); a.jump(0x34, 'check')
    a.label('reset', 1); a.push(0); a.stmem(H); a.drop(); a.push(0); a.stmem(T); a.drop()
    a.label('check', 1)
    a.ldmem(T); a.push(1); a.native(etype, 1); a.push(8); a.op(0x5b); a.jump(0x34, 'free')   # m16 not an array
    a.push(1); a.ldmem(T); a.op(0x10); a.ldloc(NOW); a.op(0x5e); a.jump(0x33, 'clear')        # m16[1] <= now: timeout
    a.ldloc(SIDE); a.ldmem(S); a.op(0x5b); a.jump(0x33, 'clear')         # same direction: nothing emerges here
    a.push(0); a.ldmem(T); a.op(0x10); a.push(1); a.native(exists, 1); a.op(0x64); a.jump(0x33, 'clear')  # holder gone
    a.ldloc(GM3); a.jump(0x34, 'deny')                                   # this gate out of sector: timer only
    a.ldloc(GM3); a.push(0); a.push(0); a.ldmem(T); a.op(0x10); a.callobj(getid, 0)
    a.push(2); a.native(dist, 2)                                         # SA_Distance(m16[0].GetObjectID(), gm3)
    a.push(CLEAR_D); a.op(0x5d); a.jump(0x34, 'deny')                    # <= D: still in the exit area
    a.label('clear', 1); a.push(0); a.stmem(T); a.drop()                 # m16 = 0
    a.label('free', 1)
    a.ldmem(H); a.jump(0x33, 'deny')                                     # held by another ship
    a.ldloc(SHIP); a.push(1); a.callself(release, 1); a.drop()           # stock: ReleaseSlot(ship) (its warp-in slot)
    a.ldloc(SHIP); a.stmem(H); a.drop(); a.push(0); a.stmem(T); a.drop(); a.ldloc(SIDE); a.stmem(S); a.drop()
    a.drop(); a.push(0); a.ret()                                         # return 0 (value above the return records)
    a.label('deny', 1); a.drop(); a.push(-1); a.ret()                    # return -1
    req, req_used = finish(a, e, end); req_dmax = a.dmax
    # ReleaseSlot(ship): forward to the controller, release the through slot into m16 = {ship, now + 60}, free the
    # warp-in slot with the stock 60 s stamp
    e2, end2, ins2 = method_range(o, 2014, 'ReleaseSlot')
    SHIP, NOW, D, I = -4, 0, 1, 1
    r = Asm(e2)
    r.emit(o.code[e2:e2 + 3], 0)
    r.push(0); r.ldglob(g); r.callobj(getage, 0)                         # now
    r.ldmem(CTRL); r.jump(0x33, 'local')                                 # controller: handle here
    r.push(0); r.callself(destgate, 0)                                   # d = GetDestinationGate()
    r.ldloc(D); r.jump(0x34, 'pop')
    r.push(0); r.ldloc(D); r.callobj(isctrl, 0); r.jump(0x34, 'pop')     # d.IsWarpController()
    r.ldloc(SHIP); r.push(1); r.ldloc(D); r.callobj(relname, 1); r.drop()  # d.ReleaseSlot(ship)
    r.label('pop', 2); r.drop()
    r.label('local', 1)
    r.ldloc(SHIP); r.ldmem(H); r.op(0x5b); r.jump(0x34, 'warpin')
    r.ldloc(NOW); r.push(60); r.op(0x46); r.ldloc(SHIP); r.emit(b'\x28\x00\x02', -1)   # {ship, now + 60}
    r.stmem(T); r.drop(); r.push(0); r.stmem(H); r.drop()               # m16 = {..}; m15 = 0
    r.label('warpin', 1); r.push(0)                                      # i = 0
    r.label('loop', 2); r.ldloc(I); r.push(20); r.op(0x5c); r.jump(0x34, 'end')
    r.ldloc(SHIP); r.ldloc(I); r.ldmem(A14); r.op(0x10); r.op(0x5b); r.jump(0x34, 'next')
    r.push(0); r.ldloc(I); r.ldmem(A14); r.emit(b'\x17', -2); r.drop()  # m14[i] = 0
    r.ldloc(NOW); r.push(STAMP_B); r.op(0x46); r.ldloc(I); r.push(20); r.op(0x46); r.ldmem(A14); r.emit(b'\x17', -2); r.drop()
    r.jump(0x32, 'end')                                                  # m14[i + 20] = now + 60
    r.label('next', 2); r.ldloc(I); r.push(1); r.op(0x46); r.stloc(I); r.drop(); r.jump(0x32, 'loop')
    r.label('end', 2); r.drop(); r.drop(); r.push(0); r.ret()
    rel, rel_used = finish(r, e2, end2)
    return [(e, req, '__requestWarpThroughSlot rewritten: %d of %d B used, max depth %d' % (req_used, len(req), req_dmax)),
            (e2, rel, 'ReleaseSlot rewritten: %d of %d B used, max depth %d' % (rel_used, len(rel), r.dmax))]


def options(o):
    """[(option, [(code_off, new_bytes, note)])] for this build."""
    mem, g = member_map(o)
    sites = {s[0]: s for s in find_sites(o, mem)}
    pw = lambda v: b'\x06' + struct.pack('>h', v)
    jt = lambda pc: struct.pack('>I', pc + 5)
    A = [(sites['A1 through cooldown'][2], bytes([5, COOLDOWN_S]), 'm16 = now + %d' % COOLDOWN_S),
         (sites['A2 warp-in stamp'][2], bytes([5, STAMP_S]), 'm14[i + 20] = now + %d' % STAMP_S),
         (sites['A3 idle wander'][2], pw(IDLE_MIN) + pw(IDLE_RAND), 'SA_Idle(m3, %d + SE_Random(%d))' % (IDLE_MIN, IDLE_RAND)),
         (sites['A4 slot poll#0'][2], pw(POLL_RAND) + pw(POLL_MIN), 'TI_WaitRandomForInterrupt(%d, %d, this)' % (POLL_MIN, POLL_RAND)),
         (sites['A4 slot poll#1'][2], pw(POLL_RAND) + pw(POLL_MIN), 'TI_WaitRandomForInterrupt(%d, %d, this)' % (POLL_MIN, POLL_RAND))]
    a5 = sites['A5 grant jf'][2]; a6 = sites['A6 m3 test'][2]
    A5 = [(a5 + 1, jt(a5), 'jf target -> next instruction: the through slot is always granted')]
    A6 = [(a6 + 1, jt(a6), 'jf target -> next instruction: same-direction exemption without the gate m3 test')]
    B = build_b(o, mem, g)
    # A7: hold position instead of the SA_Idle wander: 'ldmem m3; push2; native SA_Idle' (9 B, continuation kept)
    #     -> 'push1; native TI_Delay; push0; drop; not' (the result is dropped by the instruction after)
    e = o.find(2004, '__EnterHyperspace')[0]
    idle = next(pc for pc in o.insns(e) if o.code[pc] == 0x82 and o.operand(pc) == 'SA_Idle')
    assert o.code[idle - 4] == 0x0f and o.code[idle - 1] == 0x03
    A7 = [(idle - 4, b'\x02\x82' + struct.pack('>I', native_any(o, 'TI_Delay')) + b'\x01\x24\x64',
           'TI_Delay(%d + SE_Random(%d)) instead of SA_Idle (no wander order); old continuation %05x kept'
           % (IDLE_MIN, IDLE_RAND, idle + 5))]
    c, ln = sites['C rotate'][2], sites['C rotate'][3]
    C = [(c, b'\x02\x32' + struct.pack('>I', c + ln) + b'\x01' * (ln - 6), 'push1; jmp continuation; padding')]
    return [('A constants', A), ('A5 always grant', A5), ('A6 exemption out of sector', A6), ('A7 hold instead of wander', A[2:3] + A7),
            ('B clearance test', B), ('C no RotateToAngles', C)]


POPPUSH = {0x0b: (0, 1), 0x0d: (0, 1), 0x0e: (0, 1), 0x0f: (0, 1), 0x2e: (0, 1), 0x10: (2, 1), 0x64: (1, 1),
           0x65: (1, 1), 0x14: (0, 0), 0x16: (0, 0), 0x17: (3, 1), 0x24: (1, 0), 0x33: (1, 0), 0x34: (1, 0),
           0x32: (0, 0), 0x6e: (0, 0), 0x83: (0, 0)}
POPPUSH.update({k: (2, 1) for k in BIN})


def max_depth(o, entry):
    """Stack walk over all paths with constant tracking (argc must be a pushed constant); returns
    (max depth, depth conflicts at joins, jump targets outside the method, {pc: depth before the instruction})."""
    ins = list(o.insns(entry)); idx = {pc: i for i, pc in enumerate(ins)}; seen = {}; work = [(entry, [])]
    dmax = conflicts = outside = 0
    while work:
        pc, st = work.pop()
        while True:
            if pc in seen:
                conflicts += seen[pc] != len(st); break
            seen[pc] = len(st); op = o.code[pc]; i = idx[pc]
            pop = lambda: st.pop() if st else None
            if 1 <= op <= 4: st.append(op - 1)
            elif op in (0x05, 0x06, 0x07): st.append(int(o.operand(pc)))
            elif op in (0x82, 0x84, 0x87, 0x88, 0x85, 0x86):
                if op in (0x85, 0x86): pop()
                n = pop(); assert isinstance(n, int), ('argc not constant', hex(pc))
                for _ in range(n): pop()
                st.append(None)
            elif op == 0x23: del st[len(st) - int(o.operand(pc)):]
            elif op == 0x28:
                for _ in range(int(o.operand(pc))): pop()
                st.append(None)
            else:
                pops, pushes = POPPUSH[op]
                for _ in range(pops): pop()
                st.extend([None] * pushes)
            dmax = max(dmax, len(st))
            if op in (0x32, 0x33, 0x34):
                t = struct.unpack_from('>I', o.code, pc + 1)[0]
                if t not in idx: outside += 1
                else: work.append((t, list(st)))
                if op == 0x32: break
            if op == 0x83 or i + 1 >= len(ins): break
            pc = ins[i + 1]
    return dmax, conflicts, outside, seen


def suspend_points(o):
    e = o.find(2004, '__EnterHyperspace')[0]; lines, _ = o.track(e); dep = {pc: d for pc, d, _ in lines}
    return {pc + 5: (o.operand(pc), dep.get(pc)) for pc in o.insns(e) if o.code[pc] == 0x82 and o.operand(pc) in BLOCKING}


fails = 0
for label, cat in BUILDS:
    raw = load_member(cat); o = Obj(raw); base_susp = suspend_points(o)
    print('=' * 100); print('%s: build id %d, file %d B; CODE file offset = CODE offset + %d' % (label, o.build, len(raw), o.code_file_off))
    for opt, changes in options(o):
        p = bytearray(raw)
        print('\n-- option %s' % opt)
        for pc, new, note in changes:
            fo = pc + o.code_file_off; p[fo:fo + len(new)] = new
            e_ = max(x for x in o.entries if x <= pc); st_ = max(x for x in o.insns(e_) if x <= pc)
            if len(new) <= 16:
                print('   CODE %05x file +%07x  new %-27s %s' % (pc, fo, new.hex(' '), note))
                print('   %-29s old %s' % ('', o.decoded(st_, pc + len(new) - st_)))
            else:
                print('   CODE %05x file +%07x  %d bytes replaced (%s)' % (pc, fo, len(new), note))
                for k in range(0, len(new.rstrip(b'\x01')) + 1, 24):
                    print('      +%03x  %s' % (k, new[k:k + 24].hex(' ')))
                print('      then 01 padding to %05x' % (pc + len(new)))
        q = Obj(bytes(p)); checks = []
        checks.append(('length, header, build id unchanged', len(p) == len(raw) and p[:12] == raw[:12]))
        last = list(q.all_insns())[-1]
        checks.append(('CODE decodes to its exact end', last + q.width(last) == len(q.code)))
        starts = set(q.all_insns())
        checks.append(('all %d method entries are instruction starts' % len(q.entries), all(e in starts for e in q.entries)))
        old_starts = set(o.all_insns()); win = [(pc, pc + len(n)) for pc, n, _ in changes]
        outside = lambda s: {x for x in s if not any(a <= x < b for a, b in win)}
        checks.append(('instruction starts outside the windows unchanged', outside(old_starts) == outside(starts)))
        for cid, nm in ((2014, '__requestWarpThroughSlot'), (2014, 'ReleaseSlot'), (2004, '__EnterHyperspace')):
            e = q.find(cid, nm)[0]; last = max(q.insns(e)); mend = last + q.width(last)
            if not any(a < mend and e < b for a, b in win): continue
            _, bad = q.track(e); dm, conf, outj, _ = max_depth(q, e); fr = struct.unpack_from('>h', q.code, e + 1)[0]
            checks.append(('%s: tracker conflicts %d, depth-walk conflicts %d, jumps outside %d, max depth %d <= frame %d'
                           % (nm, bad, conf, outj, dm, fr), bad == 0 and conf == 0 and outj == 0 and dm <= fr))
        e0 = o.find(2004, '__EnterHyperspace')[0]; d0 = max_depth(o, e0)[3]; d1 = max_depth(q, e0)[3]
        kept = all(ip in d1 and d1[ip] == d0[ip] for ip in base_susp)
        gone = sorted('%05x %s' % (ip, base_susp[ip][0]) for ip in set(base_susp) - set(suspend_points(q)))
        checks.append(('every __EnterHyperspace resume IP is still an instruction start reached with the same depth'
                       + ('; natives removed: ' + ', '.join(gone) if gone else '; no suspend point removed'), kept))
        if opt.startswith('B'):
            for cid, nm in ((2014, '__requestWarpThroughSlot'), (2014, 'ReleaseSlot')):
                e = q.find(cid, nm)[0]; lines, _ = q.track(e)
                print('   patched %d::%s (pseudo-statements):' % (cid, nm))
                for pc, dd, l in lines:
                    print('      %05x d%-2d %s' % (pc, dd, l))
        for t, ok in checks:
            print('   %s  %s' % ('ok  ' if ok else 'FAIL', t)); fails += not ok
print('\nRESULT %s' % ('PASS' if fails == 0 else 'FAIL (%d)' % fails))
