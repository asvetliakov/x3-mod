#!/usr/bin/env python3
"""For each address: the enclosing function (start after int3 padding) and its direct callers.

  python3 seta_callers.py /path/x3.lst 4ee172 4edc69 ...   (local, untracked objdump listing)
"""
import bisect
import re
import sys

ins = []
for line in open(sys.argv[1]):
    m = re.match(r'\s+([0-9a-f]+):\s+(\S+)\s*(.*)$', line)
    if m:
        ins.append((int(m.group(1), 16), m.group(2), m.group(3)))
addrs = [a for a, _, _ in ins]
calls = {}
for a, op, o in ins:
    m = re.fullmatch(r'0x([0-9a-f]+)', o.strip()) if op in ('call', 'jmp') else None
    if m:
        calls.setdefault(int(m.group(1), 16), []).append((op, a))


def start(x):
    if x in calls:  # the address is itself a call target: treat it as the function start
        return x
    i = bisect.bisect_left(addrs, x)
    while i > 0 and ins[i - 1][1] not in ('int3',):
        i -= 1
    return ins[i][0]


for arg in sys.argv[2:]:
    x = int(arg, 16)
    s = start(x)
    print('0x%08x in 0x%08x; callers: %s' % (x, s, ' '.join('%s@0x%08x' % c for c in calls.get(s, [])) or '-'))
