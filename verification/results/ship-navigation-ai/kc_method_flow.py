#!/usr/bin/env python3
"""Call/constant flow of the KC movement methods quoted in docs/reverse-engineering/ship-navigation-ai.md.

Reads L/x3story.obj in memory from addon/04.cat (stock AP) and addon/07.cat (Mayhem 4), STRG/CLAS per loader
0x0049d030, instruction widths of 0x0049e1a0 (as in ../sector-transit-order/kc_transit_order.py). For each
listed method prints, in CODE order: native calls (82 name), dynamic calls (85 name), self calls by name
(87 name), static calls (86/88 class::method), and pushed integer constants (05 byte, 06 int16, 07 int32)
>= 100 or < 0. Only derived names, offsets and constants are printed; no bytecode is written.

    python3 -I verification/results/ship-navigation-ai/kc_method_flow.py > kc_method_flow.txt
"""
import struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools/analysis'))
from inspect_x3 import read_catalogue

X3 = Path('/mnt/c/X3')
METHODS = [(0x7d4, m) for m in ('CollisionWarn', '__MovePos', '__MovePosContinue', '__EscortObject', '__Idle',
                                 '__DefensiveMove', '__MoveToStation', '__MoveToSector', '__FlyToNextSector',
                                 '__EnterHyperspace', '__LandInHangar', '__MoveToHangar', 'GetSize', 'GetFormationDist',
                                 'GetFormationDistToLeader', 'SetFormationPos', 'GetMaxFormationSpeed', 'CheckSpeedLimit',
                                 'IsAutopilotActive', 'SafeUndock', 'Bounce')] + \
          [(0x816, 'CollisionWarn'), (0x7db, 'CollisionWarn'), (0x96, 'NotifyCollisionWarn')]
W = {op: 2 for op in (0x05, 0x47, 0x4c)}
W.update({op: 3 for op in (0x06, 0x0d, 0x0e, 0x0f, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x18, 0x19, 0x1a, 0x1b, 0x1c,
                           0x1d, 0x1f, 0x20, 0x21, 0x23, 0x28, 0x29, 0x2c, 0x30, 0x48, 0x4d, 0x6e)})
W.update({op: 5 for op in (0x07, 0x0b, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x49, 0x4e,
                           0x82, 0x84, 0x85, 0x86, 0x87, 0x88)})
W[0x0a] = 9


def member(cat, name):
    for e in read_catalogue(cat):
        if e['path'].lower() == name.lower():
            with open(cat.with_suffix('.dat'), 'rb') as f:
                f.seek(e['offset']); return bytes(b ^ 0x33 for b in f.read(e['size']))


def flow(label, obj):
    def sec(tag):
        a = obj.find(tag); n = struct.unpack_from('>I', obj, a + 4)[0]; return obj[a + 8:a + 8 + n]
    src = sec(b'STRG'); s = bytearray(len(src)); s[0] = ~src[0] & 255
    for i in range(1, len(src)): s[i] = (~src[i] - src[i - 1]) & 255
    s = bytes(s); c = sec(b'CLAS'); code = sec(b'CODE'); p = 0
    name = lambda o: s[o:s.find(b'\0', o)].decode('latin1') if 0 < o < len(s) and s[o - 1] == 0 else '?%x' % o

    def w():
        nonlocal p
        v = struct.unpack_from('>I', c, p)[0]; p += 4; return v
    meth = {}
    for _ in range(w()):
        cid, _par, _word, na = w(), w(), w(), w()
        for _m in range(na):
            e, nm, _l, _a = w(), w(), w(), w(); meth.setdefault(e, []).append((cid, name(nm)))
        for _v in range(w() * 4): w()
    assert p == len(c)

    def width(pc):
        op = code[pc]
        if op == 0x78: return 3 + (struct.unpack_from('>H', code, pc + 1)[0] + 2) * 4
        if op == 0x79:
            a, b = struct.unpack_from('>HH', code, pc + 1); return 5 + ((a + b) * 2 + 1) * 4
        return W.get(op, 1)
    print(f'== {label}')
    for cid, m in METHODS:
        es = [e for e, ms in meth.items() if (cid, m) in ms]
        if not es:
            print(f'{cid:#05x}::{m} absent'); continue
        e = es[0]; pc = e; out = []
        while pc < len(code) and (pc == e or pc not in meth):
            op = code[pc]
            if op in (0x82, 0x85, 0x87):
                out.append({0x82: 'N:', 0x85: 'D:', 0x87: 'S:'}[op] + name(struct.unpack_from('>I', code, pc + 1)[0]))
            elif op in (0x86, 0x88):
                v = struct.unpack_from('>I', code, pc + 1)[0]
                out.append('C:%d::%s' % meth[v][0] if v in meth else 'C:%x' % v)
            elif op in (0x05, 0x06, 0x07):
                v = {0x05: lambda: struct.unpack_from('>b', code, pc + 1)[0], 0x06: lambda: struct.unpack_from('>h', code, pc + 1)[0],
                     0x07: lambda: struct.unpack_from('>i', code, pc + 1)[0]}[op]()
                if v >= 100 or v < 0: out.append(f'#{v}')
            pc += width(pc)
        print(f'{cid:#05x}::{m} entry {e:#07x} size {pc - e}: ' + ' '.join(out))


flow('addon/04.cat L/x3story.obj (stock AP)', member(X3 / 'addon/04.cat', 'L/x3story.obj'))
flow('addon/07.cat L/x3story.obj (Mayhem 4)', member(X3 / 'addon/07.cat', 'L/x3story.obj'))
