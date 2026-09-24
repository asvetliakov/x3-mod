"""Run 80 A Q6: lod_switch rows (total, from->to), lod_switch_overflow rows, distinct bodies and nodes. usage: lod_switch_counts.py LOG"""
import re, sys, collections
n = 0; ov = 0; bodies = collections.Counter(); nodes = set(); dirs = collections.Counter()
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('lod_switch '):
        n += 1; b = re.search(r'body=(\S+)', l).group(1).split('\\')[-1]; bodies[b] += 1
        nodes.add(re.search(r'node=(\w+)', l).group(1)); dirs[re.search(r'from=(\d+) to=(\d+)', l).group(0)] += 1
    elif l.startswith('lod_switch_overflow'): ov += 1
print(f'lod_switch={n} overflow_rows={ov} distinct_bodies={len(bodies)} distinct_nodes={len(nodes)} dirs={dict(dirs)}')
for b, c in bodies.most_common(): print(f'  {b} {c}')
