#!/usr/bin/env python3
"""Pseudo-code of the KC gate-transit methods quoted in docs/reverse-engineering/ship-navigation-ai.md section 10.

Reads L/x3story.obj in memory from addon/04.cat (stock AP) and addon/07.cat (Mayhem 4) with the loader contract of
0x0049d030 and the instruction widths of 0x0049e1a0 (as kc_method_flow.py), then runs a symbolic stack tracker over
each listed method and prints one line per statement: CODE offset, stack depth, and a derived statement (calls by
name, constants, member/global slots, branch targets). No bytecode is written.

Opcode semantics used (inferred from stack balance; every listed method decodes with consistent depths at all joins):
01..04 push 0..3, 05 byte (signed), 06 int16, 07 int32, 0d n push stack slot (depth-n), 0e n global, 0f n member,
10 index (array on top, index below), 14 n store to slot (keeps value), 16 n store member (keeps), 17 indexed store,
23 n drop n, 24 drop, 28 n make array (element 0 = last pushed), 2e push this, 32 jmp, 33 jump if true, 34 jump if
false, 46 +, 4b -, 50 *, 51 /, 52 %, 53 &, 5a !=, 5b ==, 5c <, 5d >, 5e <=, 5f >= (5d/5e/5f by use), 64 not,
65 negate (by use), 82 native call (argc pushed last), 85 dynamic call (args, argc, object), 86 static call into
another class (args, argc, class id), 88 static call in own class (args, argc), 83 return, 6f/70 markers.
Argument 0 is the last pushed argument (0x00460630 reads it at args+1).

    python3 -I verification/results/ship-navigation-ai/gates/kc_gate_flow.py > kc_gate_flow.txt
"""
import gzip, re, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'tools/analysis'))
from inspect_x3 import read_catalogue

X3 = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3')
W = {op: 2 for op in (0x05, 0x47, 0x4c)}
W.update({op: 3 for op in (0x06, 0x0d, 0x0e, 0x0f, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x18, 0x19, 0x1a, 0x1b, 0x1c,
                           0x1d, 0x1f, 0x20, 0x21, 0x23, 0x28, 0x29, 0x2c, 0x30, 0x48, 0x4d, 0x6e)})
W.update({op: 5 for op in (0x07, 0x0b, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x49, 0x4e,
                           0x82, 0x84, 0x85, 0x86, 0x87, 0x88)})
W[0x0a] = 9
BIN = {0x46: '+', 0x4b: '-', 0x50: '*', 0x51: '/', 0x52: '%', 0x53: '&', 0x5a: '!=', 0x5b: '==', 0x5c: '<',
       0x5d: '>', 0x5e: '<=', 0x5f: '>='}


def member(cat, name):
    for e in read_catalogue(cat):
        if e['path'].lower() == name.lower():
            with open(cat.with_suffix('.dat'), 'rb') as f:
                f.seek(e['offset']); return bytes(b ^ 0x33 for b in f.read(e['size']))


class Obj:
    def __init__(self, raw):
        def sec(tag):
            a = raw.find(tag); n = struct.unpack_from('>I', raw, a + 4)[0]; return raw[a + 8:a + 8 + n]
        src = sec(b'STRG'); s = bytearray(len(src)); s[0] = ~src[0] & 255
        for i in range(1, len(src)): s[i] = (~src[i] - src[i - 1]) & 255
        self.s = bytes(s); c = sec(b'CLAS'); self.code = sec(b'CODE'); p = 0

        def w():
            nonlocal p
            v = struct.unpack_from('>I', c, p)[0]; p += 4; return v
        self.meth, self.parent = {}, {}
        for _ in range(w()):
            cid, par, _word, na = w(), w(), w(), w()
            self.parent[cid] = par
            for _m in range(na):
                e, nm, _l, argc = w(), w(), w(), w(); self.meth.setdefault(e, []).append((cid, self.name(nm), argc))
            for _v in range(w() * 4): w()
        assert p == len(c)

    def name(self, o):
        s = self.s
        return s[o:s.find(b'\0', o)].decode('latin1') if 0 < o < len(s) and s[o - 1] == 0 else '?%x' % o

    def find(self, cid, nm):
        return [e for e, ms in self.meth.items() if any(m[0] == cid and m[1] == nm for m in ms)]

    def width(self, pc):
        op = self.code[pc]
        if op == 0x78: return 3 + (struct.unpack_from('>H', self.code, pc + 1)[0] + 2) * 4
        if op == 0x79:
            a, b = struct.unpack_from('>HH', self.code, pc + 1); return 5 + ((a + b) * 2 + 1) * 4
        return W.get(op, 1)

    def insns(self, entry):
        pc = entry
        while pc < len(self.code):
            if pc != entry and pc in self.meth: break
            yield pc, self.code[pc]; pc += self.width(pc)

    def track(self, entry):
        """Symbolic stack walk; returns (lines, depth_conflicts)."""
        c = self.code; ins = [(pc, op) for pc, op in self.insns(entry)]; at = {pc: k for k, (pc, _) in enumerate(ins)}
        state = {entry: []}; work = [entry]; seen = set(); out = {}; bad = 0; declared = set()
        i32 = lambda pc: struct.unpack_from('>i', c, pc + 1)[0]
        i16 = lambda pc: struct.unpack_from('>h', c, pc + 1)[0]
        while work:
            pc0 = work.pop()
            if pc0 in seen: continue
            st = list(state[pc0]); k = at[pc0]
            while k < len(ins):
                pc, op = ins[k]; seen.add(pc); line = ''
                pop = lambda: st.pop() if st else '?'
                def flow(t):
                    nonlocal bad
                    if t in state and len(state[t]) != len(st): bad += 1
                    state.setdefault(t, list(st)); work.append(t)
                if 1 <= op <= 4: st.append(str(op - 1))
                elif op == 0x05: v = c[pc + 1]; st.append(str(v - 256 if v > 127 else v))
                elif op == 0x06: st.append(str(i16(pc)))
                elif op == 0x07: st.append(str(i32(pc)))
                elif op == 0x0d:
                    k_ = len(st) - i16(pc)
                    if 0 <= k_ < len(st) and k_ not in declared and st[k_] != 'p%d' % k_:
                        declared.add(k_); line = 'let p%d = %s' % (k_, st[k_])
                    st.append('p%d' % k_)
                elif op == 0x0e: st.append('g%d' % i16(pc))
                elif op == 0x0f: st.append('m%d' % i16(pc))
                elif op == 0x10: a = pop(); ix = pop(); st.append('%s[%s]' % (a, ix))
                elif op == 0x14: line = 'p%d = %s' % (len(st) - i16(pc), st[-1] if st else '?')
                elif op == 0x16: line = 'm%d = %s' % (i16(pc), st[-1] if st else '?')
                elif op == 0x17: a = pop(); ix = pop(); v = pop(); st.append(v); line = '%s[%s] = %s' % (a, ix, v)
                elif op == 0x23: n = i16(pc); del st[len(st) - n:]
                elif op == 0x24:
                    pop()
                elif op == 0x28: n = i16(pc); st.append('{%s}' % ', '.join(pop() for _ in range(n)))
                elif op == 0x2e: st.append('this')
                elif op == 0x64: st.append('!(%s)' % pop())
                elif op == 0x65: st.append('-(%s)' % pop())
                elif op in BIN: b = pop(); a = pop(); st.append('(%s %s %s)' % (a, BIN[op], b))
                elif op in (0x82, 0x85, 0x86, 0x87, 0x88):
                    v = i32(pc)
                    obj = pop() if op == 0x85 else None
                    cls = pop() if op == 0x86 else None
                    n = pop(); n = int(n) if n.lstrip('-').isdigit() else 0
                    args = [pop() for _ in range(n)]
                    if op in (0x82, 0x85, 0x87): nm = self.name(v)
                    else: nm = '%d::%s' % self.meth[v][0][:2] if v in self.meth else hex(v)
                    st.append('%s%s(%s)' % (obj + '.' if obj else '', nm, ', '.join(args))); line = 'call ' + st[-1]
                elif op == 0x32:
                    t = i32(pc); line = 'goto %05x' % t; flow(t); out[pc] = (len(st), line); break
                elif op in (0x33, 0x34):
                    v = pop(); t = i32(pc); line = 'if %s(%s) goto %05x' % ('' if op == 0x33 else 'not ', v, t); flow(t)
                elif op == 0x83: line = 'return p0'; out[pc] = (len(st), line); break
                elif op == 0x6e: line = 'frame %d' % i16(pc)
                elif op in (0x6f, 0x70): line = 'marker %02x' % op
                else: line = 'op %02x' % op
                if line: out[pc] = (len(st), line)
                k += 1
                if k < len(ins):
                    nxt = ins[k][0]
                    if nxt in seen or nxt in state:
                        flow(nxt); break
                    state[nxt] = list(st)
        return [(pc,) + out[pc] for pc in sorted(out)], bad


def page2006(cat, ids):
    for e in read_catalogue(cat):
        if e['path'].lower() == 't/0001-l044.pck':
            with open(cat.with_suffix('.dat'), 'rb') as f:
                f.seek(e['offset']); raw = bytes(b ^ 0x33 for b in f.read(e['size']))
            t = gzip.decompress(bytes(b ^ raw[0] ^ 0x1f for b in raw)).decode('utf-8', 'replace')
            i = t.find('<page id="2006"'); seg = t[i:t.find('</page>', i)]
            return {k: (re.search(r'<t id="%d">([^<]*)<' % k, seg) or [None, None])[1] for k in ids}


METHODS = [(2004, '__EnterHyperspace'), (2014, 'RequestWarpInSlot'), (2014, 'RequestWarpThroughSlot'),
           (2014, '__requestWarpThroughSlot'), (2014, 'ReleaseSlot'), (2014, 'GetWarpInPos'), (2014, 'IsWarpController'),
           (2014, 'HasDestination'), (2004, 'ReleaseFromSlot'), (402, 'GetAge'), (402, 'UpdateAge')]
norm = lambda s: re.sub(r'\b[mg]\d+\b', 'X', re.sub(r'goto [0-9a-f]+', 'goto', s))
texts = {}
for cat, label in ((X3 / 'addon/04.cat', 'stock AP'), (X3 / 'addon/07.cat', 'Mayhem 4')):
    o = Obj(member(cat, 'L/x3story.obj'))
    print('== %s L/x3story.obj (%s): CODE %d B' % (cat.relative_to(X3), label, len(o.code)))
    chain, c = [2019], 2019
    while o.parent.get(c) in o.parent and o.parent[c] not in chain: c = o.parent[c]; chain.append(c)
    print('class 2019 parent chain', chain, 'children', sorted(k for k, v in o.parent.items() if v == 2019))
    for cid, nm in METHODS + ([(2004, '__FlyToNextSector')] if label == 'stock AP' else []):
        for e in o.find(cid, nm):
            lines, bad = o.track(e)
            print('\n-- %d::%s entry %05x args %d, depth conflicts %d' % (cid, nm, e, [m for m in o.meth[e] if m[0] == cid][0][2], bad))
            for pc, dep, line in lines:
                if nm == '__FlyToNextSector' and pc > e + 0xc8: break  # tail (player warp, leave) not needed here
                print('  %05x d%-3d %s' % (pc, dep, line))
            if nm != '__FlyToNextSector': texts.setdefault(label, {})[(cid, nm)] = [norm(l) for _, _, l in lines]
    print()
for key, a in texts['stock AP'].items():
    b = texts['Mayhem 4'].get(key)
    print('%d::%s stock vs Mayhem: statements equal after masking member/global slots and offsets: %s (%d statements)'
          % (key[0], key[1], a == b, len(a)))
print('page 2006 names (13.cat t/0001-L044):', page2006(X3 / '13.cat', [2004, 2014, 2019, 2021, 2022, 2027, 2032, 2033, 2073]))
