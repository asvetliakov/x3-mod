#!/usr/bin/env python3
"""Pause test for captured bursts: per frame, camera_state translation/rotation and a hash of every
object_matrix / object_position row logged under that frame (rows carry no frame= field; they are
attributed to the last frame= seen). Identical hashes and camera across frames => game paused.
usage: pause_check.py <session.log> <frame> [frame ...]"""
import re, sys, hashlib
LOG = sys.argv[1]; W = {int(x) for x in sys.argv[2:]}
fre = re.compile(r' frame=(\d+)'); cur = None; H = {}; N = {}; CAM = {}
with open(LOG, errors='replace') as fh:
    for line in fh:
        m = fre.search(line)
        if m: cur = int(m.group(1))
        if cur not in W: continue
        t = line.split(' ', 1)[0]
        if t in ('object_matrix', 'object_position'):
            H.setdefault(cur, hashlib.sha1()).update(line.encode()); N[cur] = N.get(cur, 0) + 1
        elif t == 'camera_state':
            d = dict(re.findall(r'(\w+)=(\S+)', line)); CAM[cur] = (d.get('t'), d.get('r00'), d.get('r12'))
print('frame object_rows object_hash camera_t r00 r12')
for f in sorted(W):
    print(f, N.get(f, 0), H[f].hexdigest()[:12] if f in H else '-', *CAM.get(f, ('-', '-', '-')))
