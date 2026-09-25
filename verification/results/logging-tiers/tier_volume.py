#!/usr/bin/env python3
"""Volume per tier at 60 fps from the measured per-row sizes of run337 (row_volume.py output).
Row sizes are measured (bytes_per_line); the tier membership and cadences are the design note's
(docs/architecture/logging-tiers.md), so the tier totals are inferred from measured row sizes."""
import json, sys
rows = {r['name']: r for r in json.load(open(sys.argv[1] if len(sys.argv) > 1 else __file__.rsplit('/', 1)[0] + '/run337-rows.json'))['rows']}
B = lambda n: rows[n]['bytes_per_line']
FPS, H = 60.0, 3600.0
def mb_h(bytes_per_frame): return bytes_per_frame * FPS * H / 1e6
once_bytes = 32470  # measured: 148 once-per-session row names in run337 (run337-classes.json)
# Today, no option at all (inferred from the gates: the shadow/sun rows are unconditional per frame).
today = sum(B(n) for n in ['shadow_retention_frame', 'shadow_replay_candidates', 'shadow_replay_sun', 'shadow_alpha_casters',
                            'shadow_replay_depth', 'sun_shadow_lane_frame', 'sun_shadow_apply_frame']) \
        + B('frame_end') / 300 + B('camera_state') / 300 + B('volumetric_fog_cards') / 600
always = B('frame_end') / 300 + B('volumetric_fog_cards') / 600
perf_frame = sum(B(n) for n in ['frame_end', 'volumetric_fog_frame', 'volumetric_fog_cache_frame', 'volumetric_fog_cards', 'shadow_replay_depth', 'sun_shadow_apply_frame'])  # stride 1 under --perf
# The 60-frame family block under X3M_TELEMETRY (motion_output_frame and its companions, plus the five shadow/sun rows the note moves into it), bytes per frame at the 60 cadence.
family = ['motion_output_frame', 'hdr_frame', 'thin_vote_frame', 'fade_route_frame', 'hull_emission_frame', 'screen_emission_additive_frame', 'hull_lightmap_widen_frame',
          'hull_lightmap_far_fade_frame', 'hull_lightmap_frame', 'emission_source_gain_frame', 'original_fill_frame', 'shadow_lease_retirement',
          'shadow_retention_frame', 'shadow_replay_candidates', 'shadow_replay_sun', 'shadow_alpha_casters', 'sun_shadow_lane_frame']
perf_family = sum(rows[n]['bytes_per_frame'] for n in family) / 60
perf_window = (B('frame_timing') + B('frame_phases') + 2 * B('draw_pairs')
               + rows['frame_timing_slow']['bytes'] / 36 + rows['frame_phases_slow']['bytes'] / 36) / 300
perf_summary = (rows['telemetry_metric']['bytes'] + rows['telemetry_summary']['bytes'] + rows['engine_memory']['bytes']) / 234.4 / FPS  # 1 Hz summaries, measured bytes over 234.4 s
perf = perf_frame + perf_window + perf_summary + perf_family
debug = sum(r['bytes'] for r in rows.values() if r['lines_per_frame'] > 0.4) / 11073  # the 27 per-frame rows at stride 1, measured
print(json.dumps({
    'today_no_option_B_per_frame': round(today, 1), 'today_no_option_MB_per_h': round(mb_h(today), 1),
    'always_B_per_frame': round(always, 2), 'always_MB_per_h_plus_once': round(mb_h(always) + once_bytes / 1e6, 3),
    'perf_B_per_frame': round(perf, 1), 'perf_frame_rows': round(perf_frame, 1), 'perf_windows': round(perf_window, 1), 'perf_summaries': round(perf_summary, 1), 'perf_family_at_60': round(perf_family, 1), 'perf_MB_per_h': round(mb_h(perf), 1),
    'debug_B_per_frame': round(debug, 1), 'debug_MB_per_h': round(mb_h(debug), 1),
    'run337_measured_MB_per_h_at_47fps': round(104651234 / 234.415 * 3600 / 1e6, 1)}, indent=1))
