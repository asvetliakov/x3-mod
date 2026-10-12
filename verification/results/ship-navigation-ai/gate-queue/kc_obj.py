"""Shared reader and disassembler for the compiled KC story object L/x3story.obj (no bytes are written).

File layout (loader 0x0049d030): 'STOR', BE32 version (must be 801), BE32 build id (kept in VM+0x143c and
written into saves), then tagged sections until '/STO': CODE (BE32 size, bytes), SYMB/VARS/CLAS/PTCH (BE32
size, BE32 count, records), STRG (BE32 size, encoded bytes). Operand widths are those of 0x0049e1a0 (the
pass that byte-swaps every multi-byte operand to little endian in memory). Jump operands are absolute CODE
offsets (handler 0x004a317a: edi = [edi] + [VM+8]).

    from kc_obj import load_member, Obj
"""
import struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'tools/analysis'))
from inspect_x3 import read_catalogue

W = {op: 2 for op in (0x05, 0x47, 0x4c)}
W.update({op: 3 for op in (0x06, 0x0d, 0x0e, 0x0f, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x18, 0x19, 0x1a, 0x1b, 0x1c,
                           0x1d, 0x1f, 0x20, 0x21, 0x23, 0x28, 0x29, 0x2c, 0x30, 0x48, 0x4d, 0x6e)})
W.update({op: 5 for op in (0x07, 0x0b, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x49, 0x4e,
                           0x82, 0x84, 0x85, 0x86, 0x87, 0x88)})
W[0x0a] = 9
BIN = {0x46: '+', 0x4b: '-', 0x50: '*', 0x51: '/', 0x52: '%', 0x53: '&', 0x5a: '!=', 0x5b: '==', 0x5c: '<',
       0x5d: '>', 0x5e: '<=', 0x5f: '>='}
# Mnemonics: semantics inferred from stack balance in kc_gate_flow.py (consistent depths at every join)
MN = {0x01: 'push0', 0x02: 'push1', 0x03: 'push2', 0x04: 'push3', 0x05: 'pushb', 0x06: 'pushw', 0x07: 'pushd',
      0x0d: 'ldloc', 0x0e: 'ldglob', 0x0f: 'ldmem', 0x10: 'index', 0x14: 'stloc', 0x16: 'stmem', 0x17: 'stidx',
      0x23: 'dropn', 0x24: 'drop', 0x28: 'mkarr', 0x2e: 'this', 0x32: 'jmp', 0x33: 'jt', 0x34: 'jf',
      0x46: 'add', 0x4b: 'sub', 0x50: 'mul', 0x51: 'div', 0x52: 'mod', 0x53: 'and', 0x5a: 'ne', 0x5b: 'eq',
      0x5c: 'lt', 0x5d: 'gt', 0x5e: 'le', 0x5f: 'ge', 0x64: 'not', 0x65: 'neg', 0x82: 'native', 0x83: 'ret',
      0x85: 'callobj', 0x86: 'callcls', 0x88: 'callself', 0x6e: 'frame'}


def load_member(cat, name='L/x3story.obj'):
    cat = Path(cat)
    for e in read_catalogue(cat):
        if e['path'].lower() == name.lower():
            with open(cat.with_suffix('.dat'), 'rb') as f:
                f.seek(e['offset']); return bytes(b ^ 0x33 for b in f.read(e['size']))
    raise KeyError(name)


class Obj:
    def __init__(self, raw):
        self.raw = raw
        assert raw[:4] == b'STOR'
        self.version, self.build = struct.unpack_from('>II', raw, 4)
        self.sections = {}; p = 12
        while raw[p:p + 4] != b'/STO':
            tag = raw[p:p + 4].decode('latin1'); n = struct.unpack_from('>I', raw, p + 4)[0]
            self.sections[tag] = (p, n); p += 8 + n
        self.code_file_off = self.sections['CODE'][0] + 8          # file offset of CODE byte 0
        cp, cn = self.sections['CODE']; self.code = raw[cp + 8:cp + 8 + cn]
        sp, sn = self.sections['STRG']; src = raw[sp + 8:sp + 8 + sn]
        s = bytearray(len(src)); s[0] = ~src[0] & 255
        for i in range(1, len(src)): s[i] = (~src[i] - src[i - 1]) & 255
        self.s = bytes(s)
        kp, kn = self.sections['CLAS']; c = raw[kp + 8:kp + 8 + kn]; q = 0

        def w():
            nonlocal q
            v = struct.unpack_from('>I', c, q)[0]; q += 4; return v
        self.meth, self.parent, self.classes = {}, {}, {}
        for _ in range(w()):
            cid, par, word, na = w(), w(), w(), w()
            self.parent[cid] = par; rows = []
            for _m in range(na):
                e, nm, loc, argc = w(), w(), w(), w(); rows.append((e, self.name(nm), loc, argc))
                self.meth.setdefault(e, []).append((cid, self.name(nm), argc))
            nv = w(); mem = [(w(), w(), w(), w()) for _ in range(nv)]
            self.classes[cid] = {'word': word, 'methods': rows, 'members': mem}
        assert q == len(c)
        self.entries = sorted(self.meth)

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
            yield pc; pc += self.width(pc)

    def all_insns(self):
        pc = 1
        while pc < len(self.code):
            yield pc; pc += self.width(pc)

    def operand(self, pc):
        op, c = self.code[pc], self.code
        if op == 0x05: v = c[pc + 1]; return str(v - 256 if v > 127 else v)
        if W.get(op) == 3: return str(struct.unpack_from('>h', c, pc + 1)[0])
        if op in (0x82, 0x85, 0x87): return self.name(struct.unpack_from('>I', c, pc + 1)[0])
        if op in (0x86, 0x88, 0x84):
            v = struct.unpack_from('>I', c, pc + 1)[0]
            return '%d::%s' % self.meth[v][0][:2] if v in self.meth else hex(v)
        if op in (0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a): return '%05x' % struct.unpack_from('>I', c, pc + 1)[0]
        if W.get(op) == 5: return str(struct.unpack_from('>i', c, pc + 1)[0])
        if op == 0x0a: return '%d %d' % struct.unpack_from('>ii', c, pc + 1)
        return ''

    def line(self, pc):
        """One decoded instruction: CODE offset, file offset, width, mnemonic (opcode), operand. No raw bytes."""
        n = self.width(pc); op = self.code[pc]
        return '%05x  file+%07x  w%-2d %-8s %s' % (pc, self.code_file_off + pc, n,
                                                 '%s(%02x)' % (MN.get(op, 'op'), op), self.operand(pc))

    def decoded(self, pc, ln):
        """Decoded instructions covering [pc, pc + ln) (pc may be inside an instruction for operand-only sites)."""
        out, q = [], pc
        while q < pc + ln:
            out.append(('%s %s' % (MN.get(self.code[q], 'op%02x' % self.code[q]), self.operand(q))).strip()); q += self.width(q)
        return '; '.join(out)

    def track(self, entry):
        """Symbolic stack walk; returns (lines, depth_conflicts)."""
        c = self.code; ins = [(pc, self.code[pc]) for pc in self.insns(entry)]; at = {pc: k for k, (pc, _) in enumerate(ins)}
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
