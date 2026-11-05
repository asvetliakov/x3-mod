#!/usr/bin/env python3
"""List TBullets record field accesses in an objdump listing of X3AP.exe (local, untracked listing).

  i686-w64-mingw32-objdump -d -M intel X3AP.exe > x3.lst
  python3 tbullets_field_readers.py x3.lst [table=0x606fb8] [window=40]

Heuristic: for every load of the class-0 type-table pointer (ds:0x606fb8, TBullets, stride 0xdb8), follow the
next `window` instructions with a register -> record-offset map: the loaded register (and any register it is
added into) carries offset 0, `lea r,[...base...+d]` gives r the base's offset + d, `mov r,base` copies it. A
memory operand on a tracked register reports offset + displacement. Any other write to a tracked register drops
it. Not covered (absence claims stay [i]): record bases spilled to the stack and reloaded later (the fire path
0x00445170 keeps rec+0x44 at a frame slot), and pointers computed in another function.
Owning note: docs/reverse-engineering/weapon-effect-spawn.md.
"""
import collections
import re
import sys

path = sys.argv[1]
table = sys.argv[2] if len(sys.argv) > 2 else '0x606fb8'
window = int(sys.argv[3]) if len(sys.argv) > 3 else 40
ins = []
for line in open(path):
    m = re.match(r'\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(.*)', line)
    if m and m.group(2).strip():
        ins.append((int(m.group(1), 16), re.sub(r'\s+', ' ', m.group(2).strip())))
REG = r'\b(e[abcd]x|e[sd]i|ebp)\b'
counts = collections.Counter()
sites = collections.defaultdict(list)
for i, (a, t) in enumerate(ins):
    m = re.match(r'mov (e\w\w),(?:DWORD PTR )?ds:' + table + r'$', t)
    if not m:
        continue
    regs = {m.group(1): 0}
    for b, u in ins[i + 1:i + 1 + window]:
        op = u.split(' ', 1)[0]
        args = u.split(' ', 1)[1] if ' ' in u else ''
        dst = args.split(',', 1)[0]
        src = args.split(',', 1)[1] if ',' in args else ''
        # memory operands on tracked registers (lea computes an address, it does not access memory)
        if op != 'lea':
            for mem in re.findall(r'\[([^\]]+)\]', u):
                used = [r for r in re.findall(REG, mem) if r in regs]
                if used:
                    d = re.search(r'([+-]0x[0-9a-f]+)$', mem)
                    off = regs[used[0]] + (int(d.group(1), 16) if d else 0)
                    counts[off] += 1
                    sites[off].append(b)
        # register tracking
        if op == 'lea' and re.fullmatch(r'e\w\w', dst):
            mem = re.search(r'\[([^\]]+)\]', src)
            used = [r for r in re.findall(REG, mem.group(1)) if r in regs] if mem else []
            d = re.search(r'([+-]0x[0-9a-f]+)$', mem.group(1)) if mem else None
            if used:
                regs[dst] = regs[used[0]] + (int(d.group(1), 16) if d else 0)
            else:
                regs.pop(dst, None)
        elif op == 'add' and re.fullmatch(r'e\w\w', dst) and src in regs:
            regs[dst] = regs[src]
        elif op == 'mov' and re.fullmatch(r'e\w\w', dst):
            if src in regs:
                regs[dst] = regs[src]
            else:
                regs.pop(dst, None)
        elif re.fullmatch(r'e\w\w', dst) and op not in ('test', 'cmp', 'push', 'add', 'imul') and dst in regs:
            regs.pop(dst, None)
        if op in ('ret', 'call', 'jmp') or not regs:
            break
for off in sorted(counts):
    print(f'+0x{off:x} {counts[off]}: ' + ' '.join(f'{b:08x}' for b in sites[off][:40]))
