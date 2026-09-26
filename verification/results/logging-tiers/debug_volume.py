#!/usr/bin/env python3
"""--debug log volume for the membership after the second step of 2026-09-26, from the measured run337 row sizes.

Row sizes and rates are measured (run337-rows.json, 11,073 frames, 234.4 s); the membership is the implementation's
(src/proxy/log_tiers.h, docs/architecture/logging-tiers.md), so the sums are inferred. Also prints the session log's
buffer headroom: bytes produced per 200 ms writer interval at 60 fps against the 2 MiB half of the 4 MiB buffer.

    python3 verification/results/logging-tiers/debug_volume.py
"""
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
data = json.loads((HERE / 'run337-rows.json').read_text())
rows = {r['name']: r for r in data['rows']}
SECONDS, FPS = data['elapsed_ms'] / 1000.0, 60.0
per_frame = lambda n: rows[n]['bytes_per_frame'] if n in rows else 0.0
per_second = lambda n: rows[n]['bytes'] / SECONDS if n in rows else 0.0

# --debug: the family block every frame (X3M_MOTION_FRAME_LOG 1), camera_state every frame, the five shadow/sun state rows
# (X3M_SHADOW_ROWS), frame_end every frame. Rates as measured in run337 (a row the engine skips on some frames keeps its rate).
DEBUG_FRAME = ['motion_output_frame', 'hdr_frame', 'thin_vote_frame', 'fade_route_frame', 'hull_emission_frame', 'screen_emission_additive_frame',
               'hull_lightmap_widen_frame', 'hull_lightmap_far_fade_frame', 'hull_lightmap_frame', 'emission_source_gain_frame', 'original_fill_frame',
               'shadow_lease_retirement', 'camera_state', 'shadow_retention_frame', 'shadow_replay_candidates', 'shadow_replay_sun',
               'shadow_alpha_casters', 'sun_shadow_lane_frame', 'frame_end']
# --perf only: the fog and shadow cost rows at stride 1 (the family block at 60 and frame_end are shared with --debug).
PERF_ONLY_FRAME = ['volumetric_fog_frame', 'volumetric_fog_cache_frame', 'volumetric_fog_cards', 'shadow_replay_depth', 'sun_shadow_apply_frame']
# Telemetry (either group): time-based rows, converted to bytes per frame at 60 fps.
TIMED = ['telemetry_metric', 'telemetry_summary', 'engine_memory', 'loading_metric', 'telemetry_cursor_poll', 'taa_invalidate',
         'chase_transition_timing', 'chase_native_timing_metric', 'resource']
# 300-frame windows (frame timing is --perf; the frame phases are both groups): measured bytes over the frame count.
WINDOWS = ['frame_phases', 'frame_phases_slow', 'draw_pairs']

debug_frame = sum(per_frame(n) for n in DEBUG_FRAME)
timed = sum(per_second(n) for n in TIMED) / FPS
windows = sum(per_frame(n) for n in WINDOWS)
debug = debug_frame + timed + windows
both = debug + sum(per_frame(n) for n in PERF_ONLY_FRAME) + per_frame('frame_timing') + per_frame('frame_timing_slow')
interval = lambda b: b * FPS * 0.2
half = 2 * 1024 * 1024
result = {
    'debug_frame_rows_B_per_frame': round(debug_frame, 1), 'telemetry_timed_B_per_frame_at_60': round(timed, 1),
    'windows_B_per_frame': round(windows, 1), 'debug_B_per_frame': round(debug, 1), 'debug_MB_per_h_at_60': round(debug * FPS * 3600 / 1e6, 1),
    'debug_perf_B_per_frame': round(both, 1), 'debug_perf_MB_per_h_at_60': round(both * FPS * 3600 / 1e6, 1),
    'debug_perf_B_per_200ms': round(interval(both)), 'buffer_half_B': half,
    'headroom_factor_per_200ms': round(half / interval(both), 1),
}
print(json.dumps(result, indent=1))
