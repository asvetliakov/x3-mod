#!/usr/bin/env python3
"""Depth writers of the chosen frames: per ZWRITE=1 draw, model / node and whether the draw is routed (writes the
RT2 depth lane, so the lane depth_1_N covers it), grouped by consecutive model/node; the camera solved from the frame's
bullet-draw view-projection (c0-c3 of VS 5e484a06) and the bolt bbox's clip-w range. The 'dist' column is the
object_position (int32) / 10 distance from that camera; its unit scale is NOT verified (only the background draw's
position matched camera x10), so it is printed for ordering only.  usage: zwriters_distance.py LOG FRAME [FRAME...]"""
import sys, re, struct, itertools
import numpy as np
log, frames = sys.argv[1], set(sys.argv[2:])
kv = re.compile(r'(\w+)=(\S+)')
def fl(h): return struct.unpack('>f', bytes.fromhex(h))[0]
def i32(h): v = int(h, 16); return v - (1 << 32) if v >= 1 << 31 else v
data = {}; cur = None; rec = None
with open(log, errors='replace') as fh:
    for line in fh:
        if line.startswith('frame_begin'):
            fr = line.split('frame=')[1].split()[0]; cur = fr if fr in frames else None; rec = None
            if cur: data[cur] = {'draws': [], 'vp': None, 'bbox': []}
            continue
        if cur is None: continue
        k = line.split(' ', 1)[0]
        if k == 'draw':
            d = dict(kv.findall(line)); rec = {'i': int(d['index']), 'vs': d['vs'][:8], 'st': {}, 'pos': None, 'c': {}}; data[cur]['draws'].append(rec)
        elif k == 'bolt_copy':
            d = dict(kv.findall(line)); data[cur]['bbox'].append([float(v) for v in d['bbox'].split(',')])
        elif rec is None: continue
        elif k == 'state':
            d = dict(kv.findall(line)); rec['st'][int(d['id'])] = int(d['value'])
        elif k == 'object_context':
            d = dict(kv.findall(line)); rec['model'] = d['model']; rec['node'] = d['node']
        elif k == 'motion_route':
            d = dict(kv.findall(line)); rec['routed'] = d['routed']
        elif k == 'object_position' and rec['pos'] is None:
            rec['pos'] = [i32(b) for b in line.split('bits=')[1].split(',')]
        elif line.startswith('constant kind=vs type=f reg=') and rec['vs'] == '5e484a06':
            r = int(line.split('reg=')[1].split()[0])
            if r < 4: rec['c'][r] = [fl(x) for x in line.split('bits=')[1].split(',')]
for f in sorted(data, key=int):
    D = data[f]; b = next((r for r in D['draws'] if r['vs'] == '5e484a06' and len(r['c']) == 4), None)
    M = np.array([b['c'][i] for i in range(4)]); cam = np.linalg.solve(M[[0, 1, 3], :3], -M[[0, 1, 3], 3])
    ws = []
    for bb in D['bbox']:
        for c in itertools.product((0, 3), (1, 4), (2, 5)): ws.append((M @ np.array([bb[c[0]], bb[c[1]], bb[c[2]], 1.0]))[3])
    zw = [r for r in D['draws'] if r['st'].get(14) == 1]
    groups = []
    for r in zw:
        dist = float(np.linalg.norm(np.array(r['pos']) / 10.0 - cam)) if r['pos'] else None
        key = (r.get('model'), r.get('node'))
        if groups and groups[-1]['key'] == key: groups[-1]['n'] += 1; groups[-1]['last'] = r['i']
        else: groups.append({'key': key, 'first': r['i'], 'last': r['i'], 'n': 1, 'dist': dist, 'routed': r.get('routed')})
    unrouted = sum(1 for r in zw if r.get('routed') != '1')
    print(f"{f} cam=({cam[0]:.0f},{cam[1]:.0f},{cam[2]:.0f}) bolt_w=[{min(ws):.0f},{max(ws):.0f}] zwrite_draws={len(zw)} unrouted={unrouted}")
    for g in groups:
        print(f"   d{g['first']}-{g['last']} n={g['n']} model={g['key'][0]} node={g['key'][1]} routed={g['routed']} dist={g['dist'] and round(g['dist'])}")
