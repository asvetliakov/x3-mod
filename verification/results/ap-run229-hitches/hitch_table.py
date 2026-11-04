#!/usr/bin/env python3
"""Classify slow frames in an AP perf log into loads/sector changes and in-sector hitches,
and correlate each hitch (+-3 frames) with row kinds emitted between frame_end rows.
Usage: hitch_table.py LOG [lo_ms hi_ms]"""
import re, sys, collections, statistics
log = sys.argv[1]; lo = float(sys.argv[2]) if len(sys.argv) > 2 else 50; hi = float(sys.argv[3]) if len(sys.argv) > 3 else 500
kv = lambda s: dict(re.findall(r'(\w+)=(\S+)', s))
dt = {}; rows_after = collections.defaultdict(list)  # frame -> row kinds logged after that frame_end
slow = {}; phases = {}; windows = []; sectors = []; cur = -1
for ln in open(log, errors='replace'):
    k = ln.split(' ', 1)[0]
    if k == 'frame_end':
        d = kv(ln); cur = int(d['frame']); dt[cur] = int(d['dt_ms']); continue
    if k == 'frame_timing_slow':
        d = kv(ln); slow[int(d['frame'])] = d
    elif k == 'frame_phases_slow':
        d = kv(ln); phases[int(d['frame'])] = d
    elif k == 'frame_timing':
        d = kv(ln); windows.append(d)
    elif k == 'volumetric_fog_sector' or k == 'loading_phase':
        d = kv(ln); sectors.append((int(d.get('frame', cur)), k, d.get('reason', d.get('name'))))
    rows_after[cur].append((k, ln.rstrip()))
frames = sorted(dt)
med = statistics.median(dt.values())
sector_frames = sorted({f for f, k, r in sectors})
def since_sector(f):
    p = [s for s in sector_frames if s <= f]
    return f - p[-1] if p else None
def window_for(f):
    for w in windows:
        if int(w['frame']) - int(w['frames']) < f <= int(w['frame']): return w
print(f'frames={len(frames)} median_dt_ms={med} sector/load frames={sector_frames}')
big = [f for f in frames if dt[f] >= hi]
print('loads/large (dt>=%gms):' % hi, [(f, dt[f], since_sector(f)) for f in big])
hitch = []
for f in frames:
    if lo <= dt[f] < hi:
        nb = [dt.get(f + i) for i in (-3, -2, -1, 1, 2, 3) if dt.get(f + i) is not None]
        if nb and statistics.median(nb) < 30: hitch.append(f)
print(f'in-sector hitches ({lo}<=dt<{hi} ms, neighbour median<30 ms): {len(hitch)}')
SKIP = {'frame_end','screen_emission_additive_frame','volumetric_fog_frame','volumetric_fog_cards','volumetric_fog_cache_frame','sun_shadow_apply_frame','shadow_replay_depth'}
corr = collections.Counter(); 
for f in hitch:
    s = slow.get(f, {}); p = phases.get(f, {}); w = window_for(f) or {}
    near = collections.Counter(); lm_us = 0.0; lm_bytes = 0
    for g in range(f - 3, f + 4):
        for k, ln in rows_after.get(g - 1, []):  # rows logged during frame g (after frame_end g-1)
            if k in SKIP: continue
            near[k] += 1
            if k == 'loading_metric':
                d = kv(ln); lm_us += float(d.get('total_us', 0)); lm_bytes += int(d.get('bytes', 0))
    for k in near: corr[k] += 1
    print(f"f={f} dt={dt[f]} since_sector={since_sector(f)} present_us={s.get('present_us','-')} draws={s.get('draws','-')}/p50 {w.get('draws_p50','-')} prims={s.get('prims','-')} "
          f"draw_us={s.get('draw_us','-')} slow_call={s.get('slow_call','-')}:{s.get('slow_call_us','-')} gap_pre={s.get('gap_pre_us','-')} gap_draw={s.get('gap_draw_us','-')} "
          f"pre_render={p.get('pre_render_us','-')} scene_update={p.get('scene_update_us','-')} views={p.get('views_us','-')} setup={p.get('view_setup_us','-')} submit={p.get('view_submit_us','-')} "
          f"lm_total_us={lm_us:.0f} lm_bytes={lm_bytes} near={dict(near)}")
print('row kinds present within +-3 of N hitches:', corr.most_common(40))
