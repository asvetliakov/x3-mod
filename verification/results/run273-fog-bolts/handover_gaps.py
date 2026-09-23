#!/usr/bin/env python3
"""Run 73 B (run273) hand-over gaps A-D: the rows behind docs/architecture/fog-handover.md, "Run 73 B findings".
Usage: handover_gaps.py session.log  -> one block per case; reads only the needed rows (the log is 325 MB)."""
import re, sys
L = sys.argv[1]
kv = re.compile(r'(\w+)=(\S+)')
want_camera = {16257, 16258, 16259, 19553, 19555, 19556, 24763, 24765, 24766, 33815, 33817}
want_cache = {16258, 16259, 16305, 16306, 16307, 16308, 19553, 19555, 19556, 19587, 19596, 24763, 24765, 24766, 24843, 24847, 24848, 33817, 33859, 33860}
want_frames = {16307, 19596, 24847, 33860}
camera, cache, phases, timing, events, cards = {}, {}, {}, {}, [], {}
for line in open(L, errors='replace'):
    t = line.split(' ', 1)[0]
    if t not in ('camera_state', 'volumetric_fog_cache_frame', 'frame_phases_slow', 'frame_timing_slow', 'volumetric_fog_prefill', 'volumetric_fog_cache',
                 'volumetric_fog_handover', 'volumetric_fog_cards', 'volumetric_fog_docked'): continue
    m = re.search(r' frame=(\d+) ', line)
    if not m: continue
    f = int(m.group(1)); d = dict(kv.findall(line))
    if t == 'camera_state' and f in want_camera: camera[f] = d
    elif t == 'volumetric_fog_cache_frame' and f in want_cache: cache[f] = d
    elif t == 'frame_phases_slow' and int(d.get('pre_render_us', 0)) > 100000: phases[f] = d
    elif t == 'frame_timing_slow' and f in want_frames: timing[f] = d
    elif t in ('volumetric_fog_prefill', 'volumetric_fog_cache', 'volumetric_fog_handover', 'volumetric_fog_docked') and d.get('event', '') not in ('poll',) and f in (
            16258, 19554, 19555, 19556, 19676, 24764, 24765, 24848, 33817, 33860): events.append(line.strip() if t == 'volumetric_fog_handover' else line.strip()[:200])
    elif t == 'volumetric_fog_cards' and f in (31510, 33859, 33860, 34243, 34244): cards[f] = d


def world(d):  # camera = -t * inverse(R) for the row-vector world->view (R, t); R orthonormal here so inverse = transpose
    r = [float(d['r%d%d' % (i, j)]) for i in range(3) for j in range(3)]; t = [float(x) for x in d['t'].split(',')]
    return [-(t[0] * r[0 + c] + t[1] * r[3 + c] + t[2] * r[6 + c]) for c in range(3)]


print('# world camera (render units ~ m) at the arrival frames, from camera_state r/t')
for f in sorted(camera):
    d = camera[f]
    if d.get('valid') == '1': w = world(d); print(f, 'world=%.0f,%.0f,%.0f |r|=%.0f km' % (w[0], w[1], w[2], (w[0] ** 2 + w[1] ** 2 + w[2] ** 2) ** .5 / 1000))
    else: print(f, 'valid=0')
print('# cache_frame nodes / ready_far / upload_bytes')
for f in sorted(cache): print(f, 'nodes=%s ready_far=%s upload_bytes=%s rects=%s' % (cache[f]['nodes'], cache[f]['ready_far'], cache[f]['upload_bytes'], cache[f]['upload_rects']))
print('# frames with pre_render_us > 100 ms (engine time before BeginScene) and the slowest hooked call of the latch frames')
for f in sorted(phases): print(f, 'pre_render_ms=%d views_ms=%d' % (int(phases[f]['pre_render_us']) // 1000, int(phases[f]['views_us']) // 1000), ('slow_call_us=%s gap_pre_us=%s' % (timing[f]['slow_call_us'], timing[f]['gap_pre_us'])) if f in timing else '')
print('# node deltas: arrival -> whole latch (a first fill is 1,092,727 at the origin / 1,103,336 at 24765, window_edge_slabs.py)')
for a, b in ((16258, 16307), (24765, 24847), (33817, 33859)): print('%d->%d nodes=%d' % (a, b, int(cache[b]['nodes']) - int(cache[a]['nodes'])))
print('# A: residency epoch 19556 -> far_ready 19676 = %d frames (far_ready ms is on the 19676 row below)' % (19676 - 19556))
print('# events')
for e in events: print(e)
print('# cards rows: docked in flight (31510), docked at load (33859-33860, 34243) and after undock (34244)')
for f in sorted(cards): print(f, ' '.join('%s=%s' % (k, cards[f][k]) for k in ('observed', 'suppressed', 'refused', 'ready', 'warmup', 'mode')))
