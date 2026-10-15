#!/usr/bin/env python3
"""Run175 A, Q2 of the second triage: what happened in the summary intervals that carry the handler_us_max spikes.
Usage: python3 -I spike_window.py LOG lo1-hi1 [lo2-hi2 ...]
Per window: frame_end dt_ms top 5; counts of the row kinds that are not one-per-frame; the first 3 lines of each rare kind (cut)."""
import sys, re, collections
log = sys.argv[1]
wins = [tuple(map(int, a.split('-'))) for a in sys.argv[2:]]
PERFRAME = {'ship_trace','camera_state','thin_vote_frame','shadow_lease_retirement','screen_emission_additive_frame','motion_output_frame',
 'hdr_frame','frame_end','fade_route_frame','hull_emission_frame','engine_stage','engine_shimmer','engine_frame','volumetric_fog_frame',
 'volumetric_fog_cards','volumetric_fog_cache_frame','sun_shadow_lane_frame','sun_shadow_apply_frame','shadow_sun_frame','shadow_retention_frame',
 'shadow_replay_sun','shadow_replay_depth','shadow_replay_candidates','shadow_alpha_casters','original_fill_frame','hull_lightmap_widen_frame',
 'hull_lightmap_frame','hull_lightmap_far_fade_frame','engine_light_frame','emission_source_gain_frame'}
fre = re.compile(rb' frame=(\d+)')
cur = 0
data = {w: {'dt': [], 'kinds': collections.Counter(), 'ex': collections.defaultdict(list)} for w in wins}
with open(log, 'rb') as f:
    for line in f:
        k = line.split(b' ', 1)[0].decode('ascii', 'replace')
        if k == 'frame_end':
            m = fre.search(line); cur = int(m.group(1)) + 1  # rows after frame_end N belong to frame N+1
            for w in wins:
                if w[0] <= cur - 1 <= w[1]:
                    dt = re.search(rb'dt_ms=(\d+)', line); data[w]['dt'].append((int(dt.group(1)), cur - 1))
            continue
        for w in wins:
            if w[0] <= cur <= w[1] and k not in PERFRAME:
                d = data[w]; d['kinds'][k] += 1
                if len(d['ex'][k]) < 3: d['ex'][k].append(f"f~{cur} " + line[:260].decode('ascii', 'replace').rstrip())
for w in wins:
    d = data[w]
    print(f"== window frames {w[0]}-{w[1]}")
    print(" dt_ms top:", sorted(d['dt'], reverse=True)[:6])
    print(" kinds:", dict(d['kinds'].most_common()))
    for k, ex in d['ex'].items():
        if d['kinds'][k] <= 40:
            for e in ex: print("  ", e)
