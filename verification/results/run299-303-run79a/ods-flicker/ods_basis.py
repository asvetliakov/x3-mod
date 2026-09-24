"""Per ODS part node and burst frame: object_basis (node orientation, fixed point) and world_basis rows following the
node's first object_context row; reports whether orientation changes frame to frame (rotating/animated part) and the
world translation row. usage: ods_basis.py LOG"""
import sys, re, collections
KV = re.compile(r'(\w+)=(\S*)')
path = sys.argv[1]
parts = {}
for line in open(path, errors='replace'):
    if line.startswith('cull_census device') and 'usc_dock_e' in line:
        d = dict(KV.findall(line)); parts[d['node']] = d['body'].split('\\')[-1].replace('usc_dock_e_', '')
cur = None; seen = set(); rec = collections.defaultdict(dict)
for line in open(path, errors='replace'):
    if line.startswith('object_context device'):
        d = dict(KV.findall(line)); key = (int(d['frame']), d['node'])
        cur = key if d['node'] in parts and key not in seen else None
        if cur: seen.add(cur); rec[cur] = {'basis': [], 'world': [], 'wb': [], 'pos': None}
    elif cur and line.startswith('object_basis row'):
        rec[cur]['basis'].append(line.split('bits=')[1].strip())
    elif cur and line.startswith('object_matrix role=world row'):
        rec[cur]['world'].append(line.split('bits=')[1].strip())
    elif cur and line.startswith('object_matrix role=world_basis'):
        rec[cur]['wb'].append(line.split('bits=')[1].strip())
    elif cur and line.startswith('object_position'):
        rec[cur]['pos'] = line.split('bits=')[1].strip()
    elif line.startswith('draw device'):
        pass
bynode = collections.defaultdict(list)
for (f, n), r in sorted(rec.items()):
    bynode[n].append((f, r))
print(f'== {path}')
for n, rows in bynode.items():
    prev = None; changes = collections.Counter(); first = rows[0][0]
    for f, r in rows:
        if prev and f == prev[0] + 1:
            for k in ('basis', 'wb', 'world', 'pos'):
                if r[k] != prev[1][k]: changes[k] += 1
            changes['pairs'] += 1
        prev = (f, r)
    print(f'{parts[n]:12s} node={n} frames={len(rows)} consecutive_pairs={changes["pairs"]} changed: basis={changes["basis"]} world_basis={changes["wb"]} world={changes["world"]} node_pos={changes["pos"]}  basis0={rows[0][1]["basis"]}')
