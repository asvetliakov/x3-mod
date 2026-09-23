#!/usr/bin/env python3
"""Run 73 B, task 3: the two bullet draws of each F8 capture frame (VS 5e484a06, PS ec1f5c4a): render
states, stream/VB revision, primitive count, c0-3 rows hash, blend summary; prints only what differs between
the early (refused) and the late (admitted) draw, plus the draws between them and the clear before the first.
Usage: bullet_pair_state_diff.py SESSION_LOG FRAME[,FRAME...]"""
import sys, re
names = {15: 'ALPHATESTENABLE', 206: 'SEPARATEALPHABLENDENABLE', 207: 'SRCBLENDALPHA', 208: 'DESTBLENDALPHA', 209: 'BLENDOPALPHA',
         19: 'SRCBLEND', 20: 'DESTBLEND', 171: 'BLENDOP', 27: 'ALPHABLENDENABLE', 14: 'ZWRITEENABLE', 7: 'ZENABLE', 23: 'ZFUNC'}
frames = set(sys.argv[2].split(','))
blocks = {}; cur = None; events = {}
for raw in open(sys.argv[1], 'rb'):
    l = raw.decode('latin1').rstrip('\n')
    if l.startswith('capture_event ') and ' op=clear ' in l:
        m = re.search(r'frame=(\d+) .*after_draw=(\d+)', l)
        if m and m.group(1) in frames: events.setdefault(m.group(1), []).append(int(m.group(2)))
    if l.startswith('draw device='):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); cur = None
        if d['frame'] in frames and d['vs'] == '5e484a06672e28fb' and d['ps'] != 'ec1f5c4a2f4e1445':
            events.setdefault(d['frame'] + '_other', []).append((int(d['index']), d['ps'][:8], int(d['primitives'])))
        if d['frame'] in frames and d['vs'] == '5e484a06672e28fb' and d['ps'] == 'ec1f5c4a2f4e1445':
            cur = (d['frame'], int(d['index'])); blocks[cur] = {'primitives': d['primitives']}
        elif frames and int(d['frame']) > max(int(f) for f in frames): break
        continue
    if cur is None: continue
    b = blocks[cur]
    if l.startswith('state id='):
        k, v = re.findall(r'id=(\d+) value=(\d+)', l)[0]; b[names.get(int(k), f'rs{k}')] = v
    elif l.startswith('buffer_content kind=vertex'):
        b['vb'] = re.search(r'identity=(\d+)', l).group(1); b['vb_revision'] = re.search(r'revision=(\d+)', l).group(1)
    elif l.startswith('motion_input '):
        b['rows_hash'] = re.search(r'rows_hash=(\w+)', l).group(1)
    elif l.startswith('vertex_buffer identity='):
        b['vb_bytes'] = re.search(r'bytes=(\d+)', l).group(1)
    elif l.startswith('draw_result'):
        cur = None
for f in sorted(frames):
    pair = sorted(k for k in blocks if k[0] == f)
    if len(pair) != 2: print(f'frame {f}: {len(pair)} bullet draws'); continue
    a, b = blocks[pair[0]], blocks[pair[1]]
    same = {k: a[k] for k in a if b.get(k) == a[k] and k in ('primitives', 'vb', 'rows_hash', 'vb_bytes', 'SRCBLEND', 'DESTBLEND', 'BLENDOP', 'ZWRITEENABLE', 'ZENABLE', 'ZFUNC', 'SRCBLENDALPHA', 'DESTBLENDALPHA', 'BLENDOPALPHA')}
    diff = {k: (a.get(k), b.get(k)) for k in sorted(set(a) | set(b)) if a.get(k) != b.get(k)}
    print(f'frame {f} draws {pair[0][1]} and {pair[1][1]} (depth/colour clears after draws {events.get(f, [])}): differ {diff} same {same} other_vs_draws_index_ps_prims {events.get(f + "_other", [])}')
