#!/usr/bin/env python3
"""Stream a session log once; write the capture frames' per-draw rows as compact JSON.

Per capture frame (frames with a motion_output_depth_readback row): draw rows with the
rows that follow each draw (draw_args num_vertices, render states 7/14/15/27,
object_context node/model/lod, object_evidence parent, object_ancestry count),
object_bounds, cull_census (node -> body, s, verdict), cull_small_prop, engine_draw,
cull_small_parts_frame; plus frame_end draws and frame_phases for every frame (cost).
Usage: extract.py <session.log> <out.json>
"""
import json, re, sys
from collections import defaultdict

KV = re.compile(r'(\w+)=(\S+)')
def kv(line): return dict(KV.findall(line))

def main(log, out):
    depth = {}
    with open(log, 'r', errors='replace') as f:
        for line in f:
            if line.startswith('motion_output_depth_readback '):
                d = kv(line); depth[int(d['frame'])] = d
    frames = set(depth)
    draws = defaultdict(dict); bounds = defaultdict(dict); census = defaultdict(list)
    props = defaultdict(list); engine = defaultdict(dict); parts = {}; frame_end = {}; phases = []
    cur = None
    with open(log, 'r', errors='replace') as f:
        for line in f:
            h = line[:24]
            if h.startswith('draw device='):
                d = kv(line); fr = int(d['frame'])
                if fr in frames:
                    cur = {'index': int(d['index']), 'kind': d['kind'], 'prims': int(d['primitives']), 'vs': d.get('vs'), 'ps': d.get('ps'), 'states': {}}
                    draws[fr][cur['index']] = cur
                else:
                    cur = None
                continue
            if h.startswith('frame_end '):
                d = kv(line); frame_end[int(d['frame'])] = int(d['draws']); cur = None; continue
            if h.startswith('frame_phases '):
                d = kv(line); phases.append({k: int(d[k]) for k in ('frame', 'frames', 'dt_p50_us', 'views_p50_us', 'view_submit_p50_us', 'views_p50')}); continue
            if h.startswith('object_bounds device'):
                d = kv(line); fr = int(d['frame'])
                if fr in frames: bounds[fr][int(d['index'])] = {k: d[k] for k in d if k not in ('device', 'frame')}
                continue
            if h.startswith('cull_census device'):
                d = kv(line); fr = int(d['frame'])
                if fr in frames: census[fr].append({k: d.get(k) for k in ('view', 'node', 'model', 's', 'lod', 'verdict', 'body', 'radius', 'd')})
                continue
            if h.startswith('cull_small_prop device'):
                d = kv(line); fr = int(d['frame'])
                if fr in frames: props[fr].append(d['node'])
                continue
            if h.startswith('cull_small_parts_frame'):
                d = kv(line); fr = int(d['frame'])
                if fr in frames: parts[fr] = {k: d[k] for k in ('culled', 'dock_culled', 'threshold', 'dock_threshold')}
                continue
            if h.startswith('engine_draw device'):
                d = kv(line); fr = int(d['frame'])
                if fr in frames: engine[fr][int(d['index'])] = d.get('name')
                continue
            if cur is None: continue
            if h.startswith('state id='):
                d = kv(line)
                if d['id'] in ('7', '14', '15', '27'): cur['states'][d['id']] = int(d['value'])
            elif h.startswith('draw_args '):
                d = kv(line); cur['num_vertices'] = int(d['num_vertices']) if 'num_vertices' in d else None
            elif h.startswith('object_context '):
                d = kv(line); cur.update(node=d.get('node'), model=d.get('model'), lod=d.get('lod'), scoped=d.get('scoped'))
            elif h.startswith('object_evidence '):
                d = kv(line); cur['parent'] = d.get('parent')
            elif h.startswith('object_ancestry '):
                d = kv(line); cur['ancestry'] = int(d.get('count', 0)) if d.get('count', '').isdigit() else None
    res = {'depth': depth, 'frame_end': frame_end, 'phases': phases, 'frames': {}}
    for fr in sorted(frames):
        res['frames'][fr] = {'draws': draws[fr], 'bounds': bounds[fr], 'census': census[fr], 'props_culled': props[fr],
                             'engine': engine[fr], 'parts': parts.get(fr)}
    json.dump(res, open(out, 'w'))
    print(out, 'frames', sorted(frames), 'draws', {fr: len(draws[fr]) for fr in sorted(frames)})

if __name__ == '__main__':
    main(*sys.argv[1:3])
