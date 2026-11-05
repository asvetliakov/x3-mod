#!/usr/bin/env python3
"""Bit masks the EXE tests on the TBullets flag word (record +0x58, column 12), per site.

  python3 tbullets_flag_tests.py <objdump listing>

Uses the +0x58 sites of tbullets_field_readers.py (the class-0 table pointer ds:0x606fb8 followed within 40
instructions by a +0x58 operand on a derived register), then reports the immediate of the `test`/`and` on that
operand, or every `test`/`and` on the register it was loaded into, following it along both sides of every
conditional branch for up to 40 instructions per path until the register is written again (so the chained `test al,..` tests of 0x0044112b..0x004411bb are listed). Owning note:
docs/reverse-engineering/weapon-effect-spawn.md.
"""
import collections
import re
import subprocess
import sys
import pathlib

here = pathlib.Path(__file__).resolve().parent
out = subprocess.run([sys.executable, str(here / 'tbullets_field_readers.py'), sys.argv[1], '0x606fb8', '40'],
                     capture_output=True, text=True, check=True).stdout
sites = [int(a, 16) for line in out.splitlines() if line.startswith('+0x58 ') for a in line.split(':', 1)[1].split()]
ins, index = [], {}
for line in open(sys.argv[1]):
    m = re.match(r'\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(.*)', line)
    if m and m.group(2).strip():
        index[int(m.group(1), 16)] = len(ins)
        ins.append((int(m.group(1), 16), re.sub(r'\s+', ' ', m.group(2).strip())))
masks = collections.defaultdict(list)
for site in sorted(set(sites)):
    i = index[site]
    a, t = ins[i]
    m = re.match(r'(?:test|and) (?:BYTE|DWORD) PTR \[[^\]]*\+0x58\],(0x[0-9a-f]+)$', t)
    if m:
        masks[int(m.group(1), 16)].append(a)
        continue
    m = re.match(r'mov (e\w\w),DWORD PTR \[[^\]]*\+0x58\]$', t)
    found = False
    if m:
        reg = m.group(1)
        low = {'eax': 'al', 'ecx': 'cl', 'edx': 'dl', 'ebx': 'bl'}.get(reg, reg)
        work, seen = [(i + 1, 40)], set()
        while work:
            j, budget = work.pop()
            while budget > 0 and j < len(ins) and j not in seen:
                seen.add(j)
                b, u = ins[j]
                budget -= 1
                mm = re.match(r'(?:test|and) (' + reg + '|' + low + r'),(0x[0-9a-f]+)$', u)
                if mm:
                    masks[int(mm.group(2), 16)].append(b)
                    found = True
                    if u.startswith('and'):
                        break
                first = u.split(' ', 1)[1].split(',', 1)[0] if ' ' in u else ''
                if not mm and first in (reg, low) and not u.startswith(('cmp', 'test', 'push')):
                    break
                jm = re.match(r'(j\w+) 0x([0-9a-f]+)$', u)
                if jm and int(jm.group(2), 16) in index:
                    if jm.group(1) == 'jmp':
                        j = index[int(jm.group(2), 16)]
                        continue
                    work.append((index[int(jm.group(2), 16)], budget))
                if u.startswith(('ret', 'call')):
                    break
                j += 1
    if not found:
        masks['other'].append(a)
for k in sorted(masks, key=lambda k: (isinstance(k, str), k)):
    name = k if isinstance(k, str) else f'0x{k:x}'
    print(f'{name}: ' + ' '.join(f'{a:08x}' for a in masks[k]))
