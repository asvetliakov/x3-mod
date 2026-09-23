#!/usr/bin/env python3
"""Run 75 B (run279) projectile identity: node+0x130 bit 0x20000000 (set with 0x800000 on a class-0 TBullets
root node by the object-creation path 0x00441242, `or [node+0x130],0x20800000` from 0x004401ae) against the
drawn nodes of object_context rows (flags130 per node, one row per material submission). Prints, per model id,
distinct nodes and draw rows with and without the bit, and the flags130/flags12c values seen on bullet-bit
nodes. Usage: projectile_flag.py [LOG]"""
import sys, glob, collections
log = sys.argv[1] if len(sys.argv) > 1 else glob.glob('/tmp/x3-bottleX3-run279/session-*.log')[0]
kv = lambda s: dict(t.split('=', 1) for t in s.split() if '=' in t)
nodes = collections.defaultdict(lambda: [set(), set(), 0, 0]); vals = collections.Counter(); rows = 0
with open(log, errors='replace') as fh:
    for line in fh:
        if not line.startswith('object_context '): continue
        d = kv(line); rows += 1
        f = int(d.get('flags130', '0'), 16); m = d.get('model', '?'); bit = bool(f & 0x20000000)
        e = nodes[m]; e[0 if bit else 1].add(d.get('node')); e[2 if bit else 3] += 1
        if bit: vals[(m, d.get('flags130'), d.get('flags12c'))] += 1
print('object_context rows', rows, 'models', len(nodes))
for m, (a, b, ra, rb) in sorted(nodes.items(), key=lambda x: -len(x[1][0])):
    if a: print(f'model {m} nodes_with_bit {len(a)} nodes_without {len(b)} rows_with {ra} rows_without {rb}')
print('models with the bit on any node:', sum(1 for v in nodes.values() if v[0]))
print('model 00000205 entry:', [len(nodes['00000205'][0]), len(nodes['00000205'][1]), nodes['00000205'][2], nodes['00000205'][3]] if '00000205' in nodes else 'absent')
for k, n in vals.most_common(10): print('bit rows', k, n)
