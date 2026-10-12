"""Patch-site locator and class-2014 member map shared by gate_queue_listing.py and gate_queue_patches.py.

Sites are found by byte pattern inside the named method (each must match exactly once), so the same code serves the
stock and Mayhem objects, whose member numbers and method offsets differ.
"""
import struct


def find_sites(o, mem):
    """Patch sites by pattern; returns [(name, method, code_off, length, old_bytes, meaning)]."""
    m = lambda n: struct.pack('>h', mem[n])                       # member operand (BE16)
    out = []

    def one(cid, nm, pat, skip, ln, name, meaning, count=1):
        e = o.find(cid, nm)[0]; end = max(o.insns(e)); end += o.width(end)
        body = o.code[e:end]; hits = []; i = body.find(pat)
        while i >= 0: hits.append(i); i = body.find(pat, i + 1)
        assert len(hits) == count, (name, len(hits))
        for k, h in enumerate(hits):
            pc = e + h + skip
            assert pc in set(o.insns(e)), name
            out.append((name + ('' if count == 1 else '#%d' % k), '%d::%s' % (cid, nm), pc, ln,
                        o.code[pc:pc + ln], meaning))
    # ReleaseSlot: m16 = now + 60 (through-slot cooldown); m14[i + 20] = now + 60 (warp-in slot stamp)
    one(2014, 'ReleaseSlot', b'\x05\x3c\x46\x16' + m(16), 0, 2, 'A1 through cooldown',
        'pushb 60 in "m16 = now + 60" (through slot closed 60 s after release)')
    one(2014, 'ReleaseSlot', b'\x05\x3c\x46\x0d\x00\x02\x05\x14\x46\x0f' + m(14), 0, 2, 'A2 warp-in stamp',
        'pushb 60 in "m14[i + 20] = now + 60" (warp-in slot closed 60 s after release)')
    one(2014, 'ReleaseSlot', b'\x01\x16' + m(15) + b'\x24', 0, 5, 'B0 keep holder',
        'push0; stmem m15; drop  ("m15 = 0" at release)')
    # __EnterHyperspace waits: SA_Idle(m3, 1000 + SE_Random(10000)); TI_WaitRandomForInterrupt(1000, 10000, this) x2
    one(2004, '__EnterHyperspace', b'\x06\x03\xe8\x06\x27\x10\x02\x82', 0, 6, 'A3 idle wander',
        'pushw 1000; pushw 10000 -> SA_Idle(m3, 1000 + SE_Random(10000)) while no warp-in slot (ship in sector)')
    one(2004, '__EnterHyperspace', b'\x2e\x06\x27\x10\x06\x03\xe8\x04\x82', 1, 6, 'A4 slot poll',
        'pushw 10000; pushw 1000 -> TI_WaitRandomForInterrupt(1000, 10000, this): wait 1000 + rand(10000) ms; '
        '#0 warp-in slot (ship out of sector), #1 through slot', count=2)
    # __requestWarpThroughSlot decisions
    one(2014, '__requestWarpThroughSlot', b'\x0f' + m(16) + b'\x0d\x00\x02\x5e\x33', 0, 9, 'cooldown test',
        'ldmem m16; ldloc now; le; jt grant  ("m16 <= now")')
    one(2014, '__requestWarpThroughSlot', b'\x0d\x00\x07\x34', 3, 5, 'A6 m3 test',
        'jf after ldloc gm3 (same-direction exemption only when the requesting gate has a native object)')
    one(2014, '__requestWarpThroughSlot', b'\x01\x5b\x34', 2, 5, 'free test',
        'jf after "m15 == 0" (slot busy -> -1)')
    # the final "grant?" jf: last jf before 'ldloc ship; push1; callself ReleaseSlot'
    e = o.find(2014, '__requestWarpThroughSlot')[0]; ins = list(o.insns(e))
    k = next(i for i, pc in enumerate(ins) if o.code[pc] == 0x88)
    jf = ins[k - 3]; assert o.code[jf] == 0x34
    out.append(('A5 grant jf', '2014::__requestWarpThroughSlot', jf, 5, o.code[jf:jf + 5],
                'jf -> "return -1" (deny); retarget to the next instruction = always grant'))
    # RotateToAngles: from 'ldloc 1; push1; native SA_GetRotBeta' to the end of 'native SA_RotateToAngles'
    e = o.find(2004, '__EnterHyperspace')[0]; ins = list(o.insns(e))
    r = next(pc for pc in ins if o.code[pc] == 0x82 and o.operand(pc) == 'SA_RotateToAngles')
    b = next(pc for pc in ins if o.code[pc] == 0x82 and o.operand(pc) == 'SA_GetRotBeta')
    start = ins[ins.index(b) - 2]
    out.append(('C rotate', '2004::__EnterHyperspace', start, r + 5 - start, o.code[start:r + 5],
                'args + SA_GetRotBeta + SA_GetRotAlpha + SA_RotateToAngles(m3, a, b); continuation %05x' % (r + 5)))
    return out


def member_map(o):
    """Class-2014 slot members and the GetAge global, read from 2014::Create / __requestWarpThroughSlot."""
    e = o.find(2014, 'Create')[0]; ins = list(o.insns(e))
    k = next(i for i, pc in enumerate(ins) if o.code[pc] == 0x82 and o.operand(pc) == 'SE_ArrayAlloc')
    st = [int(o.operand(pc)) for pc in ins[k + 1:k + 16] if o.code[pc] == 0x16]
    mem = {14: st[0], 15: st[1], 16: st[2], 17: st[3], 18: st[4]}          # stock numbering -> this build
    e = o.find(2014, '__requestWarpThroughSlot')[0]
    g = next(int(o.operand(pc)) for pc in o.insns(e) if o.code[pc] == 0x0e)
    return mem, g
