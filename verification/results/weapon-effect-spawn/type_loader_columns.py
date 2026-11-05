#!/usr/bin/env python3
"""Column -> record offset trace of one class case of the type-table loader 0x00434e40 (X3AP.exe).

  python3 type_loader_columns.py <objdump listing> <case start hex> <case end hex> [--version N] [--vreg esi]

Every call to a field reader (0x4ea610 int, 0x4eafc0 16.16 fixed, 0x4ea440 text id, 0x46e400 name -> id,
0x4ea1b0 string) consumes one column; the next store `mov [ebp+X],eax` names its offset (a reader without such
a store goes to a stack buffer or a derived field). Column 7 is the first class column (columns 0-6 are common).

* File-version branches: a forward `jCC T` after `cmp <vreg>,imm` makes the fall-through block run only when the
  condition is false (and a trailing `jmp E` makes [T, E) the opposite block). With --version the inactive
  readers are dropped from the numbering; they are printed as `skipped`.
* Counted loops: a backward conditional jump with `cmp r,N` before it and `add r,S` inside repeats the readers
  of the loop N/S times (TShips: 6 x 2 columns at 0x00437640..0x00437662).
Owning note: docs/reverse-engineering/weapon-effect-spawn.md.
"""
import argparse
import re

READERS = {0x4ea610: 'int', 0x4eafc0: 'fix16', 0x4ea440: 'textid', 0x46e400: 'nameid', 0x4ea1b0: 'string'}
COND = {'jl': lambda v, k: v < k, 'jge': lambda v, k: v >= k, 'jg': lambda v, k: v > k, 'jle': lambda v, k: v <= k}

ap = argparse.ArgumentParser()
ap.add_argument('listing')
ap.add_argument('start')
ap.add_argument('end')
ap.add_argument('--version', type=lambda s: int(s, 0))
ap.add_argument('--vreg', default='esi')
ap.add_argument('--first', type=int, default=7)
a = ap.parse_args()
start, end = int(a.start, 16), int(a.end, 16)
ins = []
for line in open(a.listing):
    m = re.match(r'\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(.*)', line)
    if m and m.group(2).strip():
        addr = int(m.group(1), 16)
        if start <= addr < end:
            ins.append((addr, re.sub(r'\s+', ' ', m.group(2).strip())))
pos = {addr: i for i, (addr, _) in enumerate(ins)}
active = [True] * len(ins)
notes = {}
loops = []
for i, (addr, t) in enumerate(ins):
    m = re.match(r'(jl|jge|jg|jle) 0x([0-9a-f]+)$', t)
    if not m:
        continue
    target = int(m.group(2), 16)
    cmp = next(((j, u) for j, u in ((j, ins[j][1]) for j in range(i - 1, max(i - 4, -1), -1))
                if u.startswith('cmp ')), None)
    if cmp is None:
        continue
    cm = re.match(r'cmp (e\w\w),0x([0-9a-f]+)$', cmp[1])
    if not cm:
        continue
    reg, imm = cm.group(1), int(cm.group(2), 16)
    if target < addr and target in pos:
        body = range(pos[target], i)
        step = next((int(x.group(1), 16) for x in (re.match(r'add ' + reg + r',0x([0-9a-f]+)$', ins[j][1])
                                                    for j in body) if x), None)
        if step:
            loops.append((pos[target], i, imm // step))
        continue
    if reg != a.vreg or target not in pos:
        continue
    then_block = range(i + 1, pos[target])
    cond = COND[m.group(1)]
    notes[addr] = f'version test @{addr:08x}: cmp {reg},0x{imm:x}; {m.group(1)} 0x{target:x}'
    if a.version is not None and cond(a.version, imm):
        for j in then_block:
            active[j] = False
    last = ins[pos[target] - 1][1]
    jm = re.match(r'jmp 0x([0-9a-f]+)$', last)
    if jm and int(jm.group(1), 16) in pos and int(jm.group(1), 16) > target:
        else_block = range(pos[target], pos[int(jm.group(1), 16)])
        if a.version is not None and not cond(a.version, imm):
            for j in else_block:
                active[j] = False
events = []
for i, (addr, t) in enumerate(ins):
    m = re.match(r'call 0x([0-9a-f]+)$', t)
    if m and int(m.group(1), 16) in READERS:
        events.append(('read', i, READERS[int(m.group(1), 16)]))
    s = re.match(r'mov (?:DWORD|WORD) PTR \[ebp\+(0x[0-9a-f]+)\],(eax|ax)$', t)
    if s:
        events.append(('store', i, s.group(1)))
    if addr in notes:
        events.append(('note', i, notes[addr]))
expanded = []
k = 0
while k < len(events):
    loop = next((lp for lp in loops if lp[0] <= events[k][1] < lp[1]), None)
    if loop:
        body = [e for e in events if loop[0] <= e[1] < loop[1]]
        expanded += [(kind, i, val, f' (loop x{loop[2]}, pass {p + 1})') for p in range(loop[2])
                     for kind, i, val in body]
        k += len(body)
    else:
        expanded.append(events[k] + ('',))
        k += 1
col, pending = a.first, None


def flush():
    if pending:
        print(f'col {pending[0]:3d} {pending[1]:7s} @{pending[2]:08x} -> (no ebp store){pending[3]}')


for kind, i, val, tag in expanded:
    if kind == 'note':
        print('        ' + val)
        continue
    if kind == 'read':
        flush()
        if not active[i]:
            print(f'skipped {val:7s} @{ins[i][0]:08x} (inactive for version 0x{a.version:x})')
            pending = None
            continue
        pending = (col, val, ins[i][0], tag)
        col += 1
    elif kind == 'store' and pending and active[i]:
        print(f'col {pending[0]:3d} {pending[1]:7s} @{pending[2]:08x} -> +{val}{pending[3]}')
        pending = None
flush()
