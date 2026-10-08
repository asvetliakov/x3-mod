#!/usr/bin/env python3
"""Direct call sites of the engine functions the engine-side occlusion cull depends on, from an
`i686-w64-mingw32-objdump -d -M intel` listing of X3AP.exe (local, untracked), plus the readers of
bit 2 of node+0x12c that matter for the skip (test/and with an immediate 2 on +0x12c).
Usage: call_sites.py <full.s>
"""
import re, sys
TARGETS = {'0x47cfe0': 'cull/LOD pass', '0x47e780': 'per-scene root walker', '0x47b800': 'per-view transform walk',
           '0x47bc20': 'per-view state build', '0x47c840': 'camera/viewport/clear', '0x47d9c0': 'render traversal',
           '0x4c4fc0': 'per-node submit', '0x4c0150': 'material draw'}
calls = {t: [] for t in TARGETS}
bit2 = []
for line in open(sys.argv[1]):
    m = re.match(r'\s*([0-9a-f]+):\t[^\t]*\t(.*)$', line)
    if not m: continue
    addr, ins = m.groups()
    c = re.match(r'call\s+(0x[0-9a-f]+)$', ins.strip())
    if c and c.group(1) in calls: calls[c.group(1)].append('0x00' + addr)
    if re.search(r'\+0x12c\],0x2$', ins.strip()) and ins.split()[0] in ('test', 'or', 'and'):
        bit2.append(f'0x00{addr} {ins.split()[0]}')
for t, name in TARGETS.items():
    print(f'{t} {name}: {len(calls[t])} direct calls: {" ".join(calls[t])}')
print(f'immediate 0x2 ops on +0x12c: {len(bit2)}: {" ".join(bit2)}')
