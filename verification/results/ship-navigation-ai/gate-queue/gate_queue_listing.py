#!/usr/bin/env python3
"""Decoded listings of the KC gate-queue methods and the patch sites, for stock AP and Mayhem 4.

Reads L/x3story.obj in memory from addon/04.cat (stock AP 3.3) and addon/07.cat (Mayhem 4, the installed override),
prints per build: header (version, build id, SHA-256 of the decoded file, section map), the class-2014 member map,
full instruction listings of the slot methods, windows of 2004::__EnterHyperspace, the suspend points of
__EnterHyperspace (continuation IP and stack depth after every native call), and a table of patch sites located by
byte pattern (each pattern must match exactly once inside its method). Nothing is written to disk.

    python3 -I verification/results/ship-navigation-ai/gate-queue/gate_queue_listing.py [X3 dir] > gate_queue_listing.txt
"""
import datetime, hashlib, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from kc_obj import Obj, load_member
from gate_sites import find_sites, member_map

X3 = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3')
BUILDS = [('stock AP', X3 / 'addon/04.cat'), ('Mayhem 4', X3 / 'addon/07.cat')]
FULL = [(2014, 'RequestWarpInSlot'), (2014, 'RequestWarpThroughSlot'), (2014, '__requestWarpThroughSlot'),
        (2014, 'ReleaseSlot'), (2004, 'ReleaseFromSlot')]
BLOCKING = {'SA_Idle', 'TI_WaitRandomForInterrupt', 'TI_Delay', 'TI_DelayRandom', 'SA_RotateToAngles',
            'SA_FlyObjectBeforeContainer', 'SA_FlyObjectBeforeContainerPos', 'SA_FlyObjectIntoContainer',
            'SA_WarpObjectAway', 'SA_AvoidObjectCollision'}


for label, cat in BUILDS:
    raw = load_member(cat); o = Obj(raw)
    print('=' * 100)
    print('%s: %s:L/x3story.obj  %d B  SHA-256 %s' % (label, cat.relative_to(X3), len(raw), hashlib.sha256(raw).hexdigest()))
    print('  version %d, build id %d (%s UTC), CODE %d B at file offset %d (file offset = CODE offset + %d)' % (
        o.version, o.build, datetime.datetime.fromtimestamp(o.build, datetime.UTC).strftime('%Y-%m-%d %H:%M'),
        len(o.code), o.code_file_off, o.code_file_off))
    print('  sections: ' + ', '.join('%s@%d+%d' % (t, p, n) for t, (p, n) in o.sections.items()))
    if 'PTCH' in o.sections:
        p, n = o.sections['PTCH']; cnt = struct.unpack_from('>I', raw, p + 8)[0]; q = p + 12; ids = []
        for _ in range(cnt):
            pid, c2 = struct.unpack_from('>II', raw, q); q += 8 + c2 * 8; ids.append(pid)
        print('  PTCH: %d save-remap tables keyed by older build ids %d..%d' % (cnt, min(ids), max(ids)))
    else:
        print('  PTCH: none (a save whose build id differs from %d cannot be loaded with this object)' % o.build)
    mem, g = member_map(o)
    print('  class 2014 members (stock name -> this build): ' + ', '.join('m%d->m%d' % kv for kv in mem.items()) +
          '; GetAge global g%d' % g)
    acc = {}
    for e_, ms in o.meth.items():
        for m_ in ms:
            if m_[0] != 2014: continue
            for pc in o.insns(e_):
                if o.code[pc] in (0x0f, 0x16) and int(o.operand(pc)) in mem.values():
                    acc.setdefault(m_[1], set()).add('%s m%s' % ('ld' if o.code[pc] == 0x0f else 'st', o.operand(pc)))
    print('  subclasses of 2014: %s; slot-member accesses: %s' % (
        [k for k in o.parent if o.parent[k] == 2014] or 'none',
        '; '.join('%s %s' % (k, ' '.join(sorted(v))) for k, v in sorted(acc.items()))))
    print('  class 2014 entries: ' + ', '.join('%s %05x' % (m[1], e) for e in o.entries for m in o.meth[e]
                                                if m[0] == 2014 and m[1] in {n for _, n in FULL} | {'GetWarpInPos', 'GetDestinationGate', 'IsWarpController', 'Create'}))
    if label != 'stock AP':
        S = Obj(load_member(BUILDS[0][1]))
        for cid, nm in FULL + [(2004, '__EnterHyperspace'), (2014, 'Create'), (2014, 'GetWarpInPos')]:
            a = [(S.code[p], S.operand(p)) for p in S.insns(S.find(cid, nm)[0])]
            b = [(o.code[p], o.operand(p)) for p in o.insns(o.find(cid, nm)[0])]
            diff = sorted({'%02x %s->%s' % (x[0], x[1], y[1]) for x, y in zip(a, b)
                           if x != y and x[0] not in (0x32, 0x33, 0x34, 0x84, 0x86, 0x88)})
            print('  %d::%s: %d instructions, opcode sequence equal to stock: %s; other operand differences: %s' % (
                cid, nm, len(b), [x[0] for x in a] == [y[0] for y in b], ', '.join(diff) or 'none'))
        print('  (full listings: see the stock section; jump/call operands differ only by the method offsets)')
    for cid, nm in FULL if label == 'stock AP' else []:
        e = o.find(cid, nm)[0]
        print('\n-- %d::%s entry %05x (file +%07x)' % (cid, nm, e, e + o.code_file_off))
        for pc in o.insns(e): print('   ' + o.line(pc))
    e = o.find(2004, '__EnterHyperspace')[0]; ins = list(o.insns(e))
    lines, bad = o.track(e); depth = {pc: d for pc, d, _ in lines}
    if label != 'stock AP': marks_only = True
    else: marks_only = False
    print('\n-- 2004::__EnterHyperspace entry %05x: windows (slot requests and waits, rotate step); tracker conflicts %d' % (e, bad))
    marks = [pc for pc in ins if o.code[pc] in (0x82, 0x85) and o.operand(pc) in
             ('RequestWarpInSlot', 'RequestWarpThroughSlot', 'SA_RotateToAngles', 'SA_IsWarpOutSlotAvailable')]
    shown = set()
    for mpc in ([] if marks_only else marks):
        k = ins.index(mpc)
        for pc in ins[max(0, k - 12):k + 40]:
            if pc not in shown: shown.add(pc); print('   ' + o.line(pc))
        print('   ...')
    print('\n-- 2004::__EnterHyperspace suspend points (continuation IP after each native call; the saved task IP when '
          'it blocks)')
    for pc in ins:
        if o.code[pc] == 0x82:
            nm = o.operand(pc)
            if nm in BLOCKING:
                print('   %05x  -> resume at %05x  %-32s depth before call %s' % (pc, pc + 5, nm, depth.get(pc, '?')))
    print('\n-- patch sites (located by pattern, unique inside the method)')
    for name, meth, pc, ln, old, meaning in find_sites(o, mem):
        e = o.find(*{'2014::' + n: (2014, n) for n in ('ReleaseSlot', '__requestWarpThroughSlot')}.get(meth, (2004, '__EnterHyperspace')))[0]
        start = max(p for p in o.insns(e) if p <= pc)                    # instruction containing the site
        print('   %-20s %-30s CODE %05x file +%07x len %-2d | %s' % (name, meth, pc, pc + o.code_file_off, ln,
              o.decoded(start, pc + ln - start) if ln <= 12 else o.decoded(start, ln)[:150] + ' ...'))
        print('   %-20s %s' % ('', meaning))
    print()
