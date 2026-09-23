"""Run 268: camera and object pose delta between two F8 frames for given nodes.
Camera: camera_state r00..r22 and t. Node: object_matrix role=world / world_basis rows following the node's first
object_context row in each frame. Prints camera rotation angle and translation, node basis rotation angle,
node world position delta, and camera-to-node distance per frame.
Camera position from role=view (row-vector convention, c = -t R^T); relative rotation = world_basis x view rotation.
usage: view_delta.py LOG FRAME_A FRAME_B NODE..."""
import sys, re, struct
import numpy as np
log, fa, fb, nodes = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
F = {fa, fb}; kv = re.compile(r'(\w+)=(\S+)')
cam = {}; W = {}; cur = None; rows = {}
def fl(h): return struct.unpack('<f', bytes.fromhex(h)[::-1])[0]
with open(log, errors='replace') as fh:
    for line in fh:
        if line.startswith('camera_state '):
            d = dict(kv.findall(line))
            if d.get('frame') in F:
                cam[d['frame']] = (np.array([[float(d[f'r{i}{j}']) for j in range(3)] for i in range(3)]), np.array([float(x) for x in d['t'].split(',')]))
        elif line.startswith('object_context '):
            d = dict(kv.findall(line)); cur = None
            if d.get('frame') in F and d.get('node') in nodes and (d['frame'], d['node']) not in W:
                cur = (d['frame'], d['node']); rows = {}
        elif cur and line.startswith('object_matrix '):
            d = dict(kv.findall(line))
            rows.setdefault(d['role'], {})[int(d['row'])] = [fl(b) for b in d['bits'].split(',')]
            if 'view' in rows and len(rows['view']) == 4 and 'world_basis' in rows and len(rows['world_basis']) == 4:
                W[cur] = {r: np.array([rows[r][k] for k in range(4)]) for r in rows}; cur = None
def ang(A, B):
    R = A.T @ B; c = (np.trace(R) - 1) / 2; return np.degrees(np.arccos(np.clip(c, -1, 1)))
Ra, ta = cam[fa]; Rb, tb = cam[fb]
print(f'camera {fa}->{fb}: rotation {ang(Ra, Rb):.2f} deg, camera_state t delta {np.linalg.norm(tb - ta):.1f} units (t {ta.round(1)} -> {tb.round(1)})')
for n in nodes:
    a, b = W.get((fa, n)), W.get((fb, n))
    if a is None or b is None: print(f'node {n}: missing world rows ({a is not None}, {b is not None})'); continue
    Ba = np.array([a['world_basis'][i][:3] for i in range(3)]); Bb = np.array([b['world_basis'][i][:3] for i in range(3)])
    pa = np.array(a['world'][3][:3]); pb = np.array(b['world'][3][:3])
    Va = np.array(a['view']); Vb = np.array(b['view'])
    qa = np.append(pa, 1) @ Va; qb = np.append(pb, 1) @ Vb
    ca = -Va[3, :3] @ Va[:3, :3].T; cb = -Vb[3, :3] @ Vb[:3, :3].T
    print(f'node {n}: view-matrix camera rotation {ang(Va[:3,:3], Vb[:3,:3]):.2f} deg, camera move {np.linalg.norm(cb - ca):.1f}; '
          f'node view pos {qa[:3].round(0)} -> {qb[:3].round(0)} (dist {np.linalg.norm(qa[:3]):.0f} -> {np.linalg.norm(qb[:3]):.0f}); '
          f'object rotation relative to camera {ang(Ba @ Va[:3,:3], Bb @ Vb[:3,:3]):.2f} deg')
    print(f'node {n}: world_basis rotation {ang(Ba, Bb):.2f} deg; world pos delta {np.linalg.norm(pb - pa):.1f}; '
          f'basis-in-camera rotation {ang(Ba @ Ra, Bb @ Rb):.2f} deg / {ang(Ba @ Ra.T, Bb @ Rb.T):.2f} deg')
