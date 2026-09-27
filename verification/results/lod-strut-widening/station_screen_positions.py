"""Screen position of every station node drawn in a captured frame: the world matrix translation (object_matrix
role=world row 3 after each object_context of the frame) through the logged view and projection matrices
(row-vector D3D convention), joined to the cull_census body name of the same node. Names the station blobs of
station_blobs.py. Usage: python3 station_screen_positions.py <session.log> <frame> [W H]"""
import re, struct, sys
import numpy as np
log, frame = sys.argv[1], sys.argv[2]
W, H = (int(sys.argv[3]), int(sys.argv[4])) if len(sys.argv) > 4 else (5120, 1440)
f32 = lambda h: struct.unpack('>f', bytes.fromhex(h))[0]
bodies, dist = {}, {}
rx = re.compile(r'node=([0-9a-f]+) .*? d=(\d+) radius=(\d+).*? lod=(\d+) verdict=(\w+).*? body=(\S+)')
cur, mats, nodes = None, {}, {}
with open(log, errors='replace') as fh:
    for line in fh:
        if line.startswith('cull_census ') and f'frame={frame} ' in line:
            m = rx.search(line)
            if m:
                bodies[m.group(1)] = m.group(6); dist[m.group(1)] = (int(m.group(2)), int(m.group(3)), m.group(4), m.group(5))
        elif line.startswith('object_context '):
            if cur is not None and len(mats) == 3:
                nodes.setdefault(cur, mats)
            cur, mats = (re.search(r'node=([0-9a-f]+)', line).group(1) if f'frame={frame} ' in line else None), {}
        elif cur is not None and line.startswith('object_matrix '):
            m = re.match(r'object_matrix role=(\w+) row=(\d) bits=(\S+)', line)
            if m and m.group(1) in ('world', 'view', 'projection'):
                mats.setdefault(m.group(1), np.zeros((4, 4)))[int(m.group(2))] = [f32(h) for h in m.group(3).split(',')]
if cur is not None and len(mats) == 3:
    nodes.setdefault(cur, mats)
rows = []
for node, m in nodes.items():
    body = bodies.get(node, '?')
    if not body.lower().startswith('stations'):
        continue
    p = np.array([0, 0, 0, 1.0]) @ m['world'] @ m['view'] @ m['projection']
    if p[3] <= 0:
        continue
    x, y = (p[0] / p[3] * 0.5 + 0.5) * W, (0.5 - p[1] / p[3] * 0.5) * H
    d, r, lod, verdict = dist.get(node, (0, 0, '?', '?'))
    rows.append((x, y, p[3], body, node, d, r, lod, verdict))
for x, y, w, body, node, d, r, lod, verdict in sorted(rows):
    print(f'x {x:7.0f} y {y:6.0f} w {w:9.0f} {body:58s} node {node} d {d} ({d/505/1000:.1f} km) r {r} lod {lod} {verdict}')
