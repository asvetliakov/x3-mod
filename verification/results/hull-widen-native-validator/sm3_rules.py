#!/usr/bin/env python3
"""Minimal ps_3_0 bytecode rule check (subset of the native d3d9 validator rules,
DX9 SDK 'ps_3_0 registers' read-port table and instruction remarks).

usage: sm3_rules.py <program.bin>... [--json out.json] [--disasm]
Prints one row per violation: file, instruction index, dword offset, rule, text.
Rules: opcode length field == parameter tokens; <= 1 distinct c# per instruction
(read port 1; def excluded); <= 1 distinct v#; <= 3 distinct r#; dcl/def before
other instructions; register ranges (r < 32, c < 224, v < 10, s < 16); texld* source
types (src0 r/v/c, sampler s#, texldd gradients r/v/c), no neg/abs on texld*
sources; declared samplers; read of never-written r# components (linear, path
insensitive: a component counts as written after any earlier write).
"""
import json, struct, sys

NAMES = {0: 'nop', 1: 'mov', 2: 'add', 3: 'sub', 4: 'mad', 5: 'mul', 6: 'rcp', 7: 'rsq', 8: 'dp3', 9: 'dp4',
         10: 'min', 11: 'max', 12: 'slt', 13: 'sge', 14: 'exp', 15: 'log', 18: 'lrp', 19: 'frc', 25: 'call',
         26: 'callnz', 27: 'loop', 28: 'ret', 29: 'endloop', 30: 'label', 31: 'dcl', 32: 'pow', 33: 'crs',
         34: 'sgn', 35: 'abs', 36: 'nrm', 37: 'sincos', 38: 'rep', 39: 'endrep', 40: 'if', 41: 'ifc', 42: 'else',
         43: 'endif', 44: 'break', 45: 'breakc', 46: 'mova', 65: 'texkill', 66: 'texld', 81: 'def', 88: 'cmp',
         90: 'dp2add', 91: 'dsx', 92: 'dsy', 93: 'texldd', 94: 'setp', 95: 'texldl', 96: 'breakp'}
NO_DST = {25, 26, 27, 28, 29, 30, 38, 39, 40, 41, 42, 43, 44, 45, 96, 65}
TEX = {66, 93, 95}
REPLICATE = {6, 7, 14, 15}           # scalar source ops
RT = {0: 'r', 1: 'v', 2: 'c', 3: 'a', 5: 'oRast', 6: 'o', 7: 'i', 8: 'oC', 9: 'oDepth', 10: 's', 14: 'b',
      15: 'aL', 17: 'vMisc', 18: 'l', 19: 'p'}


def rtype(t):
    return ((t >> 28) & 7) | ((t >> 8) & 0x18)


def swz(t):
    return [(t >> (16 + 2 * k)) & 3 for k in range(4)]


def fmt_src(t):
    mod = (t >> 24) & 15
    s = ''.join('xyzw'[c] for c in swz(t))
    s = '' if s == 'xyzw' else '.' + s
    name = f"{RT.get(rtype(t), '?')}{t & 0x7ff}{s}"
    return {0: name, 1: '-' + name, 11: f'|{name}|', 12: f'-|{name}|'}.get(mod, f'{name}<mod{mod}>')


def fmt_dst(t):
    m = (t >> 16) & 15
    mods = ('_sat' if t & 0x100000 else '') + ('_pp' if t & 0x200000 else '')
    mk = '' if m == 15 else '.' + ''.join('xyzw'[k] for k in range(4) if m >> k & 1)
    return mods, f"{RT.get(rtype(t), '?')}{t & 0x7ff}{mk}"


def parse(words):
    out, at = [], 1
    while at < len(words):
        t = words[at]
        if t == 0xffff:
            out.append((at, 'end', [])); break
        op = t & 0xffff
        if op == 0xfffe:
            at += ((t >> 16) & 0x7fff) + 1; continue
        n = (t >> 24) & 15
        out.append((at, op, words[at + 1:at + 1 + n]))
        at += n + 1
    return out


def disasm(op, p):
    if op == 81:
        return f"def c{p[0] & 0x7ff}, " + ', '.join(f"{struct.unpack('<f', struct.pack('<I', x))[0]:.6g}" for x in p[1:])
    if op == 31:
        return f"dcl[{p[0]:08x}] {fmt_dst(p[1])[1]}"
    name = NAMES.get(op, f'op{op}')
    if op in NO_DST or not p:
        return name + ' ' + ', '.join(fmt_src(x) for x in p)
    mods, d = fmt_dst(p[0])
    return f"{name}{mods} {d}, " + ', '.join(fmt_src(x) for x in p[1:])


def check(words):
    rows, written, samplers, seen_body = [], {}, set(), False
    expected = {1: 2, 2: 3, 3: 3, 4: 4, 5: 3, 6: 2, 7: 2, 8: 3, 9: 3, 10: 3, 11: 3, 12: 3, 13: 3, 14: 2, 15: 2,
                18: 4, 19: 2, 32: 3, 35: 2, 36: 2, 88: 4, 90: 4, 91: 2, 92: 2, 93: 5, 66: 3, 95: 3, 81: 5, 31: 2,
                42: 0, 43: 0, 39: 0, 29: 0, 28: 0, 44: 0, 38: 1, 40: 1, 41: 2, 27: 2, 65: 1}
    if words[0] != 0xffff0300:
        return [(0, 0, 'skipped-not-ps_3_0', f'{words[0]:08x}')]
    for i, (at, op, p) in enumerate(parse(words)):
        if op == 'end':
            continue
        text = disasm(op, p)
        if op in expected and len(p) != expected[op]:
            rows.append((i, at, 'length', f'{text} (length field {len(p)}, expected {expected[op]})'))
        if op == 31:
            if seen_body: rows.append((i, at, 'dcl-after-body', text))
            if rtype(p[1]) == 10: samplers.add(p[1] & 0x7ff)
            continue
        if op == 81:
            if seen_body: rows.append((i, at, 'def-after-body', text))
            if (p[0] & 0x7ff) >= 224: rows.append((i, at, 'range', text))
            continue
        seen_body = True
        has_dst = op not in NO_DST and len(p) > 0
        srcs = p[1:] if has_dst else p
        regs = {}
        for s in srcs:
            if not s & 0x80000000: rows.append((i, at, 'token', text))
            regs.setdefault(rtype(s), set()).add(s & 0x7ff)
        for ty, lim, name in ((2, 1, 'const-read-port'), (1, 1, 'input-read-port'), (0, 3, 'temp-read-port')):
            if len(regs.get(ty, ())) > lim:
                rows.append((i, at, name, f"{text} ({len(regs[ty])} distinct {RT[ty]}#, max {lim})"))
        for s in p:
            ty, num = rtype(s), s & 0x7ff
            if (ty == 0 and num >= 32) or (ty == 2 and num >= 224) or (ty == 1 and num >= 10) or (ty == 10 and num >= 16):
                rows.append((i, at, 'range', text))
        if op in TEX:
            if rtype(srcs[0]) not in (0, 1, 2): rows.append((i, at, 'tex-src0', text))
            if rtype(srcs[1]) != 10: rows.append((i, at, 'tex-sampler', text))
            elif (srcs[1] & 0x7ff) not in samplers: rows.append((i, at, 'sampler-undeclared', text))
            if op == 93 and any(rtype(g) not in (0, 1, 2) for g in srcs[2:4]): rows.append((i, at, 'texldd-grad', text))
            if any((s >> 24) & 15 for s in srcs): rows.append((i, at, 'tex-modifier', text))
            if (p[0] >> 16) & 15 != 15: rows.append((i, at, 'info-tex-partial-mask', text))
        # uninitialized temp reads
        if has_dst:
            m = (p[0] >> 16) & 15
            for k, s in enumerate(srcs):
                if rtype(s) != 0: continue
                if op in TEX:
                    comps = {0, 1} if k in (0, 2, 3) else set()
                elif op == 90: comps = {swz(s)[0], swz(s)[1]} if k < 2 else {swz(s)[0]}
                elif op == 8: comps = {swz(s)[c] for c in range(3)}
                elif op == 9: comps = set(swz(s))
                elif op in REPLICATE: comps = {swz(s)[0]}
                else: comps = {swz(s)[c] for c in range(4) if m >> c & 1}
                missing = comps - written.get(s & 0x7ff, set())
                if missing:
                    rows.append((i, at, 'uninitialized-read', f"{text} (r{s & 0x7ff}.{''.join('xyzw'[c] for c in sorted(missing))})"))
            if rtype(p[0]) == 0:
                written.setdefault(p[0] & 0x7ff, set()).update(c for c in range(4) if m >> c & 1)
    return rows


def main(argv):
    files = [a for a in argv if not a.startswith('--')]
    out = {}
    for f in files:
        words = list(struct.unpack(f'<{len(open(f, "rb").read()) // 4}I', open(f, 'rb').read()))
        rows = check(words)
        out[f] = [{'instruction': i, 'dword': at, 'rule': r, 'text': t} for i, at, r, t in rows]
        for i, at, r, t in rows:
            print(f"{f.rsplit('/', 1)[-1]}\t{i}\t{at}\t{r}\t{t}")
        if '--disasm' in argv:
            for i, (at, op, p) in enumerate(parse(words)):
                if op != 'end': print(f"  {i:4d} {at:5d} {disasm(op, p)}")
    if '--json' in argv:
        json.dump(out, open(argv[argv.index('--json') + 1], 'w'), indent=1)


if __name__ == '__main__':
    main(sys.argv[1:])
