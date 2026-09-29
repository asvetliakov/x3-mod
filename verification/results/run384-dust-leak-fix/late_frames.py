#!/usr/bin/env python3
"""Run 109 A (run384) late-flight frame cost and draw-count series (answers 1-3 of the late-frames triage).
Usage: python3 late_frames.py <session log> [x3m-regenerate.log]  -> writes late_frames.json next to this script (< 50 KB).
Sector frames = frame_end draws > SECTOR_DRAWS (menu frames draw 56)."""
import sys, json, statistics as st
from pathlib import Path
SECTOR_DRAWS, BIN = 150, 1000
log = Path(sys.argv[1]); out = Path(__file__).with_name('late_frames.json')
def kv(l): return dict(p.split('=', 1) for p in l.split()[1:] if '=' in p)
def f(x):
    try: return float(x)
    except (TypeError, ValueError): return None
def pct(v, q):
    v = sorted(x for x in v if x is not None)
    return round(v[min(len(v) - 1, int(q * len(v)))], 3) if v else None
fe = []; rows = {k: [] for k in ('loop_phases', 'frame_timing', 'frame_phases', 'frame_timing_slow', 'frame_phases_slow',
    'sun_shadow_apply_frame', 'shadow_replay_depth', 'volumetric_fog_frame', 'motion_output_frame', 'hdr_frame',
    'screen_emission_additive_frame', 'cull_small_props_frame', 'lens_flare_gain_frame', 'draw_batch', 'engine_memory',
    'scene_graph_census', 'collide_census', 'draw_pairs')}
tm = []; cur_tm_frame = None
for l in open(log, errors='replace'):
    k = l.split(' ', 1)[0]
    if k == 'frame_end':
        d = kv(l); fe.append((int(d['frame']), int(d['draws']), int(d['issued']), float(d['dt_ms'])))
    elif k in rows: rows[k].append(kv(l))
    elif k == 'telemetry_summary':
        d = kv(l); cur_tm_frame = int(d['frame']) if d.get('device') == '1' else cur_tm_frame
    elif k == 'telemetry_metric':
        d = kv(l)
        if d.get('device') == '1' and cur_tm_frame is not None: tm.append((cur_tm_frame, d['name'], int(d['count']), float(d['total_us'])))
sector = [r for r in fe if r[1] > SECTOR_DRAWS]
res = {'log': str(log), 'frame_end_rows': len(fe), 'sector_frames': len(sector), 'sector_draws_threshold': SECTOR_DRAWS,
       'last_frame': fe[-1][0], 'last_sector_frame': sector[-1][0]}
def summ(rs):
    return {'n': len(rs), 'dt_p50': pct([r[3] for r in rs], .5), 'dt_p95': pct([r[3] for r in rs], .95),
            'draws_p50': pct([r[1] for r in rs], .5), 'issued_p50': pct([r[2] for r in rs], .5), 'issued_p95': pct([r[2] for r in rs], .95)}
# 1. per-1000-frame series (sector frames only) and last 200 sector frames
bins = []
for b in range(0, fe[-1][0] + 1, BIN):
    s = [r for r in sector if b <= r[0] < b + BIN]
    if s: bins.append({'from': b, **summ(s)})
res['bins_1000_sector'] = bins
last = sector[-200:]; res['last_200_sector'] = {'from': last[0][0], 'to': last[-1][0], **summ(last)}
# per-bin rank correlation of dt vs issued within the late part
def spearman(a, b):
    ra = {i: r for r, i in enumerate(sorted(range(len(a)), key=lambda i: a[i]))}; rb = {i: r for r, i in enumerate(sorted(range(len(b)), key=lambda i: b[i]))}
    n = len(a); return round(1 - 6 * sum((ra[i] - rb[i]) ** 2 for i in range(n)) / (n * (n * n - 1)), 3) if n > 2 else None
late = [r for r in sector if r[0] >= sector[-1][0] - 3000]
res['late3000_spearman_dt_vs_issued'] = spearman([r[3] for r in late], [r[2] for r in late])
# dt in issued-count classes, early vs late (does the same draw load cost more late?)
def by_issued(rs):
    out = {}
    for lo, hi in ((150, 300), (300, 450), (450, 600), (600, 10 ** 6)):
        v = [r[3] for r in rs if lo <= r[2] < hi]
        if v: out[f'{lo}-{hi}'] = {'n': len(v), 'dt_p50': pct(v, .5)}
    return out
res['dt_by_issued_frames_1000_4000'] = by_issued([r for r in sector if 1000 <= r[0] < 4000])
res['dt_by_issued_last3000'] = by_issued(late)
# 1/2. loop_phases, frame_timing, frame_phases per 300-frame window
LP = ('input_p50_us', 'region_p50_us', 'cutevent_p50_us', 'containers_p50_us', 'collide_p50_us', 'simulate_p50_us', 'post_p50_us',
      'passb_p50_us', 'sweep_p50_us', 'sum_p50_us', 'self_p50_us', 'collide_p95_us', 'containers_p95_us', 'region_max_owner')
res['loop_phases'] = [{'frame': int(d['frame']), **{c.replace('_p50_us', ''): (d[c] if c == 'region_max_owner' else int(d[c])) for c in LP if c in d}} for d in rows['loop_phases']]
FT = ('dt_p50_us', 'dt_p95_us', 'draws_p50', 'draw_p50_us', 'draw_native_p50_us', 'scene_p50_us', 'gap_pre_p50_us', 'gap_draw_p50_us', 'gap_post_p50_us', 'gap_draw_per_draw_us', 'state_calls_p50', 'slow')
res['frame_timing'] = [{'frame': int(d['frame']), **{c: f(d.get(c)) for c in FT}} for d in rows['frame_timing']]
FP = ('pre_render_p50_us', 'views_p50_us', 'view_setup_p50_us', 'view_submit_p50_us', 'overlays_p50_us', 'begin_scene_p50_us', 'scene_end_p50_us', 'views_p50')
res['frame_phases'] = [{'frame': int(d['frame']), **{c: f(d.get(c)) for c in FP}} for d in rows['frame_phases']]
# first vs last sector window deltas and rank correlations over sector windows (draws_p50 > SECTOR_DRAWS, region > 500 us)
idx = [i for i, d in enumerate(rows['frame_timing']) if f(d.get('draws_p50')) and f(d['draws_p50']) > SECTOR_DRAWS]
lpm = {d['frame']: d for d in res['loop_phases']}; fpm = {d['frame']: d for d in res['frame_phases']}
win = [(res['frame_timing'][i], lpm.get(res['frame_timing'][i]['frame']), fpm.get(res['frame_timing'][i]['frame'])) for i in idx]
win = [w for w in win if w[1] and w[2] and w[1]['region'] > 500]
def row(w):
    t, l, p = w
    return {'frame': t['frame'], 'dt_p50_us': t['dt_p50_us'], 'draws_p50': t['draws_p50'], 'input_pre_render_us': l['input'], 'region_us': l['region'],
            'input_outside_region_us': l['input'] - l['region'], 'cutevent_us': l['cutevent'], 'containers_us': l['containers'], 'collide_us': l['collide'],
            'passb_us': l['passb'], 'self_us': l['self'], 'views_us': p['views_p50_us'], 'view_submit_us': p['view_submit_p50_us'], 'overlays_us': p['overlays_p50_us'],
            'gap_draw_us': t['gap_draw_p50_us'], 'draw_us': t['draw_p50_us'], 'gap_draw_per_draw_us': t['gap_draw_per_draw_us'], 'state_calls_p50': t['state_calls_p50']}
res['first_last_sector_window'] = [row(win[0]), row(win[-1])]
res['window_spearman'] = {'n': len(win), 'input_vs_draws': spearman([w[1]['input'] for w in win], [w[0]['draws_p50'] for w in win]),
    'input_vs_frame': spearman([w[1]['input'] for w in win], [w[0]['frame'] for w in win]),
    'views_vs_draws': spearman([w[2]['views_p50_us'] for w in win], [w[0]['draws_p50'] for w in win]),
    'dt_vs_draws': spearman([w[0]['dt_p50_us'] for w in win], [w[0]['draws_p50'] for w in win])}
# slow rows: counts per 3000 frames and late medians / slow_call owners
def slow(kind, fields):
    rs = rows[kind]; o = {'total': len(rs), 'per_3000': {}}
    for d in rs: b = int(d['frame']) // 3000 * 3000; o['per_3000'][b] = o['per_3000'].get(b, 0) + 1
    lr = [d for d in rs if int(d['frame']) >= sector[-1][0] - 3000]; o['late3000_n'] = len(lr)
    for c in fields: o['late3000_' + c + '_p50'] = pct([f(d.get(c)) for d in lr], .5)
    return o, lr
o, lr = slow('frame_timing_slow', ('dt_us', 'draws', 'gap_pre_us', 'gap_draw_us', 'draw_us', 'slow_call_us'))
calls = {}
for d in lr: calls[d.get('slow_call')] = calls.get(d.get('slow_call'), 0) + 1
o['late3000_slow_call'] = calls; res['frame_timing_slow'] = o
res['frame_phases_slow'] = slow('frame_phases_slow', ('dt_us', 'pre_render_us', 'views_us', 'view_submit_us', 'overlays_us', 'views'))[0]
# 3. proxy passes, per 1000-frame bin over sector frames
sec_frames = {r[0] for r in sector}
def pass_bins(kind, fields, per_frame=True):
    o = []
    for b in range(0, fe[-1][0] + 1, BIN):
        rs = [d for d in rows[kind] if b <= int(d['frame']) < b + BIN and (not per_frame or int(d['frame']) in sec_frames)]
        if rs: o.append({'from': b, 'n': len(rs), **{c: pct([f(d.get(c)) for d in rs], .5) for c in fields}})
    return o
res['sun_shadow_apply_us'] = pass_bins('sun_shadow_apply_frame', ('us', 'applied'))
res['shadow_replay_depth'] = pass_bins('shadow_replay_depth', ('us', 'draws', 'draws3', 'draws4', 'apply_us'))
fog = rows['volumetric_fog_frame']; rs = {}
for d in fog: rs[(d.get('applied'), d.get('reason'))] = rs.get((d.get('applied'), d.get('reason')), 0) + 1
res['volumetric_fog_frame_applied_reason'] = {f'{a}/{r}': n for (a, r), n in rs.items()}
res['motion_output'] = pass_bins('motion_output_frame', ('draws', 'routed', 'fill_us', 'taa_run_us', 'taa_draw_us'))
res['hdr_frame'] = pass_bins('hdr_frame', ('redirect_us', 'writeback_us', 'meter_us', 'readback_us'))
res['screen_emission_admitted_nonzero_frames'] = sum(1 for d in rows['screen_emission_additive_frame'] if d.get('admitted') != '0')
res['cull_small_props'] = [{'frame': int(d['frame']), **{c: int(d[c]) for c in ('draws', 'culled', 'kept', 'no_scope')}} for d in rows['cull_small_props_frame']]
res['lens_flare_gain'] = [{'frame': int(d['frame']), 'draws': int(d['draws']), 'skipped': int(d['skipped'])} for d in rows['lens_flare_gain_frame']][-12:]
res['draw_batch'] = [{'frame': int(d['frame']), 'draws': int(d['draws']), 'same_mesh': int(d['same_mesh']), 'same_material': int(d['same_material'])} for d in rows['draw_batch']]
res['engine_nodes'] = [{'frame': int(d['frame']), 'engine_nodes': d.get('engine_nodes')} for d in rows['scene_graph_census']]
res['collide_p1_pairs_sum'] = [{'frame': int(d['frame']), 'p1_pairs_sum': int(d['p1_pairs_sum']), 'p1_rejected_sum': int(d['p1_rejected_sum'])} for d in rows['collide_census']]
em = rows['engine_memory']; res['engine_memory_note'] = 'engine_memory rows are cumulative read counters (reads/queries/hits), no memory size'
res['engine_memory_reads_first_last'] = [em[1].get('reads') if len(em) > 1 else None, em[-1].get('reads')]
# proxy telemetry_metric (device 1), p50 of per-call us per 1000-frame bin for the main proxy costs
names = ('taa_run', 'hdr_writeback', 'hdr_meter', 'route_fill', 'stretch_backend', 'lock_wait', 'present_normal')
tmb = {}
for fr, n, c, t in tm:
    if n in names and c: tmb.setdefault(n, {}).setdefault(fr // BIN * BIN, []).append(t / c)
res['telemetry_per_call_us_p50'] = {n: {str(b): pct(v, .5) for b, v in sorted(d.items())} for n, d in tmb.items()}
# 4. draw_pairs: per-frame counts of the top VS/PS pairs (per 300-frame window, all frames incl. menu)
PAIRS = {'d5e1c75351ed3f04/8360f422de08b5bd': 'effects EG (flares/glows)', '4944d81dfe531b37/3006f8030a467739': 'Split BUMP hull',
         '4944d81dfe531b37/ca6bfa4a6cca7e2a': 'hull BUMPMAP (argon)', '4944d81dfe531b37/5e0a10fe752b6140': 'argon BUMP alpha-tested',
         '53a0a641107ed76c/8759c7838bbc86c2': 'VS3/PS3 material 53a0/8759'}
dp = []
for d in rows['draw_pairs']:
    top = dict((t.rsplit(':', 1)[0], int(t.rsplit(':', 1)[1])) for t in d.get('top', '').split(',') if ':' in t)
    dp.append({'frame': int(d['frame']), 'draws_per_frame': round(int(d['draws']) / 300, 1), **{v: round(top.get(k, 0) / 300, 1) for k, v in PAIRS.items()}})
res['draw_pairs_per_frame'] = dp
# 4b. optional: Split ship bodies in the install's x3m-regenerate.log texel-rule table (ELIGIBLE = baked, else refuse=<reason>)
if len(sys.argv) > 2:
    import re
    split = {}
    for l in open(sys.argv[2], errors='replace'):
        m = re.match(r'\[\S+\]\s+\|\s{3}(ships/\S*split\S*) .*?thr=(\S+) .*?(ELIGIBLE|refuse=\S+)', l, re.I)
        if m and '/props/' not in m.group(1).lower() and 'wreck' not in m.group(1).lower():
            split[m.group(1)] = {'thr': m.group(2), 'verdict': 'baked' if m.group(3) == 'ELIGIBLE' else m.group(3)[7:]}
    res['split_ship_bodies'] = split
    res['split_ship_bodies_by_verdict'] = {v: sum(1 for x in split.values() if x['verdict'] == v) for v in sorted({x['verdict'] for x in split.values()})}
def columns(rs): return {c: [r.get(c) for r in rs] for c in (rs[0] if rs else {})}
for k in ('loop_phases', 'frame_timing', 'frame_phases', 'draw_batch', 'cull_small_props', 'collide_p1_pairs_sum', 'engine_nodes', 'lens_flare_gain', 'draw_pairs_per_frame'):
    res[k] = columns(res[k])  # columnar: one list per field, index = window
for k in ('frame_timing', 'frame_phases'):
    res[k] = {c: [int(x) if isinstance(x, float) and x.is_integer() else x for x in v] for c, v in res[k].items()}
json.dump(res, open(out, 'w'), separators=(',', ':'))
print(out, out.stat().st_size, 'bytes')
