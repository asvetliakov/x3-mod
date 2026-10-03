#!/usr/bin/env python3
"""List instructions that access fields of the object at *(0x00606f34) (the game-state object).

Read-only over a local, untracked objdump listing:
  i686-w64-mingw32-objdump -d -M intel --no-show-raw-insn X3AP.exe > /path/x3.lst
  python3 seta_field_access.py /path/x3.lst 0xcc 0xd0 0x718 0x720 [--writes] [--lookback N] [--globals 0x609104 ...]

A field access counts when its base register was loaded from ds:0x606f34 within the previous
LOOKBACK instructions of the same function and not overwritten in between (heuristic; each row
is to be checked in the listing). --globals prints every instruction naming the given absolute
addresses, with the direction (write when the address is the first operand).
"""
import re
import sys

LOOKBACK = 24
INSN = re.compile(r'^\s+([0-9a-f]+):\s+(\S+)\s*(.*)$')
LOAD = re.compile(r'^(e[a-ds][xip]|e[sd]i),(?:DWORD PTR )?ds:0x606f34$')
WRITE_OPS = {'mov', 'add', 'sub', 'and', 'or', 'xor', 'inc', 'dec', 'imul', 'shl', 'shr', 'sar', 'neg', 'not',
             'movs', 'fstp', 'fist', 'fistp', 'cmpxchg', 'xchg', 'adc', 'sbb'}


def main():
    args = sys.argv[1:]
    path = args.pop(0)
    writes_only = '--writes' in args
    global LOOKBACK
    if '--lookback' in args:
        i = args.index('--lookback')
        LOOKBACK = int(args[i + 1])
        del args[i:i + 2]
    globals_ = []
    if '--globals' in args:
        i = args.index('--globals')
        globals_ = [int(a, 16) for a in args[i + 1:]]
        args = args[:i]
    fields = [int(a, 16) for a in args if not a.startswith('--')]
    window = []  # (addr, op, operands)
    counts = {}
    for line in open(path):
        m = INSN.match(line)
        if not m:
            if line.strip().endswith('>:'):
                window = []
            continue
        addr, op, ops = int(m.group(1), 16), m.group(2), m.group(3).split('#')[0].strip()
        for g in globals_:
            if re.search(r'ds:0x%x\b' % g, ops):
                kind = 'W' if ops.startswith('DWORD PTR ds:0x%x' % g) or ops.startswith('ds:0x%x,' % g) \
                    or ops.startswith('BYTE PTR ds:0x%x' % g) else 'R'
                if op in ('cmp', 'test', 'push'):
                    kind = 'R'
                print('global 0x%x %s 0x%08x %s %s' % (g, kind, addr, op, ops))
        for f in fields:
            fm = re.search(r'\[(e[a-ds][xip]|e[sd]i)\+0x%x\]' % f, ops)
            if not fm:
                continue
            reg = fm.group(1)
            base = None
            for (a2, op2, ops2) in reversed(window[-LOOKBACK:]):
                lm = LOAD.match(ops2) if op2 == 'mov' else None
                if lm and lm.group(1) == reg:
                    base = a2
                    break
                if ops2.split(',')[0] == reg and op2 not in ('cmp', 'test', 'push'):
                    break
            if base is None:
                continue
            is_write = ops.split(',')[0].endswith('[%s+0x%x]' % (reg, f)) and op in WRITE_OPS
            if op in ('fstp', 'fist', 'fistp'):
                is_write = True
            if writes_only and not is_write:
                continue
            counts[(f, is_write)] = counts.get((f, is_write), 0) + 1
            print('field +0x%x %s 0x%08x %s %s  (base loaded at 0x%08x)' % (f, 'W' if is_write else 'R', addr, op,
                                                                           ops, base))
        window.append((addr, op, ops))
        if len(window) > 4 * LOOKBACK:
            window = window[-LOOKBACK:]
    for (f, w), n in sorted(counts.items()):
        print('count +0x%x %s %d' % (f, 'W' if w else 'R', n))


if __name__ == '__main__':
    main()
