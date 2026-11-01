#!/usr/bin/env python3
"""Stream an x3m.log once and extract per-draw records for the F8 capture frames.

Usage: extract_draws.py LOG OUT.json
Writes draws (vs/ps/node/model/lod/bounds/textures/alpha states), camera_state,
motion_output_frame TAA fields and hull/thin frame rows for frames that have an
hdr_readback row. Output is large-ish (kept local, not tracked)."""
import json, re, sys

KV = re.compile(r'(\w+)=(\S+)')
STATES = {'7': 'zenable', '14': 'zwrite', '15': 'alphatest', '24': 'alpharef', '25': 'alphafunc',
          '27': 'alphablend', '19': 'srcblend', '20': 'destblend', '22': 'cull', '206': 'sepalpha'}
FRAME_ROWS = ('motion_output_frame', 'thin_vote_frame', 'hull_lightmap_frame', 'hull_lightmap_widen_frame',
              'hull_lightmap_far_fade_frame', 'hdr_frame', 'motion_output_cut', 'camera_state',
              'sun_shadow_apply_frame', 'original_fill_frame', 'hull_emission_frame')

def kv(line):
    return dict(KV.findall(line))

def main(log, out):
    frames = set()
    with open(log, 'rb') as f:
        for raw in f:
            if raw.startswith(b'hdr_readback '):
                frames.add(int(kv(raw.decode('latin1'))['frame']))
    lo, hi = min(frames) - 2, max(frames)
    draws, bounds, frame_rows, widen = {}, {}, {}, {}
    cur = None
    with open(log, 'rb') as f:
        for raw in f:
            head = raw.split(b' ', 1)[0]
            if head == b'draw':
                d = kv(raw.decode('latin1'))
                fr = int(d.get('frame', -1))
                if d.get('device') == '1' and fr in frames:
                    cur = {'frame': fr, 'index': int(d['index']), 'vs': d['vs'], 'ps': d['ps'],
                           'prims': int(d['primitives']), 'tex': {}, 'texdesc': {}, 'st': {}, 'samp0': {}}
                    draws[(fr, cur['index'])] = cur
                else:
                    cur = None
                continue
            if head in (b'frame_end', b'frame_begin'):
                pass
            line = None
            if cur is not None:
                if head == b'object_context':
                    d = kv(raw.decode('latin1'))
                    for k in ('node', 'node_handle', 'model', 'lod', 'mesh', 'flags12c', 'flags130'):
                        cur[k] = d.get(k)
                elif head == b'object_position':
                    cur['pos_bits'] = kv(raw.decode('latin1'))['bits']
                elif head == b'object_matrix':
                    cur['scale_bits'] = kv(raw.decode('latin1')).get('bits')
                elif head == b'texture':
                    d = kv(raw.decode('latin1'))
                    cur['tex'][d['stage']] = d['identity']
                elif head == b'texture_desc':
                    d = kv(raw.decode('latin1'))
                    cur['texdesc'][d['stage']] = '%sx%s/%s' % (d['w'], d['h'], d['format'])
                elif head == b'state':
                    d = kv(raw.decode('latin1'))
                    if d['id'] in STATES:
                        cur['st'][STATES[d['id']]] = int(d['value'])
                elif head == b'motion_input':
                    d = kv(raw.decode('latin1'))
                    cur['mi_blockers'] = d.get('blockers')
                elif head == b'draw_result':
                    cur = None
            if head == b'object_bounds' or head == b'hull_lightmap_widen_draw' or head == b'hull_lightmap_widen_held':
                d = kv(raw.decode('latin1'))
                fr = int(d.get('frame', -1))
                if fr in frames:
                    if head == b'object_bounds':
                        bounds[(fr, int(d['index']))] = d
                    else:
                        widen[(fr, int(d['index']))] = head.decode() + ' ' + ' '.join('%s=%s' % (k, d[k]) for k in ('stage', 'reason', 'size', 'c') if k in d)
            elif head.decode('latin1') in FRAME_ROWS:
                d = kv(raw.decode('latin1'))
                fr = int(d.get('frame', -1))
                if lo <= fr <= hi and (fr in frames or head == b'camera_state'):
                    frame_rows.setdefault(str(fr), {})[head.decode()] = d
    res = []
    for key, dr in sorted(draws.items()):
        b = bounds.get(key)
        if b:
            dr['box'] = [float(b[k]) for k in ('sx0', 'sy0', 'sx1', 'sy1')] + [float(b['zmin']), float(b['zmax'])]
        if key in widen:
            dr['widen'] = widen[key]
        res.append(dr)
    json.dump({'frames': sorted(frames), 'draws': res, 'frame_rows': frame_rows}, open(out, 'w'))
    print('frames', sorted(frames), 'draws', len(res), 'bounds', len(bounds))

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
