#!/usr/bin/env python3
"""Per-draw producer breakdown of captured frames from a DLL session log (--debug capture rows).
Every `draw` row is a game draw that entered the proxy's hooks (ctx.draws); the proxy's own passes
use saved native vtable slots and never emit `draw` rows. Categories (first match wins):
  background  before the frame's first depth-writing draw, no depth write (sky sphere, nebula layers)
  game_post   after scene_end_marker on a render target other than the main one (the game's own glow downsample)
  hud         after scene_end_marker on the main target (brackets, icons, gauges, text)
  offscreen   before scene_end_marker on another render target
  additive    ALPHABLENDENABLE=1 and DESTBLEND=ONE
  blended     ALPHABLENDENABLE=1 otherwise
  opaque_node ZWRITE=1, no blend, object_context scoped=1 (a scene-graph node)
  opaque_other ZWRITE=1, no blend, no node scope (background, sky, planets)
  other       everything else (no depth write, no blend: e.g. nebula/background cards)
Usage: draw_breakdown.py <session.log> <first_frame> <last_frame> [--detail]"""
import sys, collections
path, lo, hi = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]); detail = '--detail' in sys.argv
draws = []; cur = None; scene_end = {}; frame_end = {}; replay = {}; fog = {}
def kv(line):
    return dict(p.split('=', 1) for p in line.split()[1:] if '=' in p)
with open(path, errors='replace') as fh:
    for line in fh:
        k = line[:line.find(' ')]
        if k == 'draw':
            d = kv(line); f = int(d['frame'])
            cur = None
            if lo <= f <= hi:
                cur = dict(frame=f, index=int(d['index']), kind=d['kind'], prims=int(d['primitives']), vs=d['vs'], ps=d['ps'],
                           states={}, posT=False, fvf=None, rt0=None, rtw=None, scoped=0, model=None, node=None, lod=None, routed=None)
                draws.append(cur)
            continue
        if k == 'frame_end':
            d = kv(line); f = int(d['frame'])
            if lo <= f <= hi: frame_end[f] = int(d['draws'])
            cur = None; continue
        if k == 'scene_end_marker':
            d = kv(line); f = int(d['frame'])
            if lo <= f <= hi: scene_end.setdefault(f, int(d['draw_index']))
            continue
        if k == 'shadow_replay_depth':
            d = kv(line); f = int(d['frame'])
            if lo <= f <= hi: replay[f] = d
            continue
        if k == 'volumetric_fog_frame':
            d = kv(line); f = int(d['frame'])
            if lo <= f <= hi: fog[f] = d
            continue
        if cur is None: continue
        if k == 'state':
            d = kv(line); cur['states'][int(d['id'])] = int(d['value'])
        elif k == 'vertex_element':
            if ' usage=9 ' in line: cur['posT'] = True
        elif k == 'vertex_buffer':
            d = kv(line); cur['fvf'] = int(d.get('fvf', '0'))
        elif k == 'surface' and ' role=rt0 ' in line:
            d = kv(line); cur['rt0'] = d['identity']; cur['rtw'] = f"{d['width']}x{d['height']}"
        elif k == 'object_context':
            d = kv(line); cur['scoped'] = int(d.get('scoped', 0)); cur['model'] = d.get('model'); cur['node'] = d.get('node'); cur['lod'] = d.get('lod')
        elif k == 'motion_route':
            d = kv(line); cur['routed'] = int(d.get('routed', 0))
main_rt = collections.Counter(d['rt0'] for d in draws).most_common(1)[0][0] if draws else None
first_zw = {}
for d in draws:
    if d['states'].get(14, 0) and d['frame'] not in first_zw: first_zw[d['frame']] = d['index']
def cat(d):
    s = d['states']
    if d['frame'] in scene_end and d['index'] > scene_end[d['frame']]:
        return 'hud' if d['rt0'] == main_rt else 'game_post'
    if d['posT'] or (d['fvf'] is not None and d['fvf'] & 0x4 and not d['fvf'] & 0x2): return 'hud'
    if d['rt0'] != main_rt: return 'offscreen'
    if d['index'] < first_zw.get(d['frame'], 1 << 30) and not s.get(14, 0): return 'background'
    if s.get(27, 0):
        return 'additive' if s.get(20) == 2 else 'blended'
    if s.get(14, 0) and s.get(7, 1): return 'opaque_node' if d['scoped'] else 'opaque_other'
    return 'other'
by = collections.defaultdict(collections.Counter); up = collections.defaultdict(collections.Counter)
models = collections.defaultdict(collections.Counter)
lods = collections.defaultdict(collections.Counter); nodes = collections.defaultdict(collections.Counter)
for d in draws:
    c = cat(d); by[d['frame']][c] += 1
    if c == 'opaque_node':
        lods[d['frame']]['lod0' if d['lod'] == '00000000' else 'lod>0'] += 1; nodes[d['frame']][(d['node'], d['model'], d['lod'])] += 1
    if d['kind'].endswith('up'): up[d['frame']][c] += 1
    if c == 'opaque_node': models[d['frame']][d['model']] += 1
order = ['background', 'opaque_node', 'opaque_other', 'blended', 'additive', 'other', 'offscreen', 'game_post', 'hud']
print(f'main_rt_identity={main_rt}')
for f in sorted(by):
    n = sum(by[f].values()); r = replay.get(f, {}); g = fog.get(f, {})
    print(f'frame={f} logged_draws={n} frame_end_draws={frame_end.get(f)} scene_end_marker={scene_end.get(f)} '
          + ' '.join(f'{c}={by[f][c]}({100*by[f][c]/n:.0f}%,up={up[f][c]})' for c in order)
          + f" | proxy_native: shadow_replay_issues={r.get('issues')} per_cascade={','.join(r.get(f'draws{i}','-') for i in range(5))}"
          f" casters={r.get('replayed')} fog_applied={g.get('applied')} fog_calls={g.get('calls')}"
          f" opaque_models={len(models[f])} opaque_nodes={len(nodes[f])} opaque_by_lod={dict(lods[f])}"
          f" nodes_ge5_draws={sum(1 for v in nodes[f].values() if v >= 5)} draws_in_those={sum(v for v in nodes[f].values() if v >= 5)}"
          f" top_nodes={[(m, l, v) for (n, m, l), v in nodes[f].most_common(4)]}")
if detail:
    for d in draws:
        if d['frame'] == lo:
            s = d['states']
            print(d['index'], cat(d), d['kind'], d['prims'], d['vs'][:8], d['ps'][:8], d['rtw'], d['rt0'], 'zw', s.get(14), 'ab', s.get(27), s.get(19), s.get(20), 'node', d['scoped'], d['node'], d['model'], d['lod'], 'routed', d['routed'])
