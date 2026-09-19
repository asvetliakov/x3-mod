# Shader-pair coverage census (unregistered/unmatched motion_route pairs)

Census of every `motion_route` row logged with `routed=0` and `unmatched!=none`
across the 60 preserved session logs `/tmp/x3-bottleX3-run{116..175}/session-*.log`
(≈9.6 GB total). Streamed with `grep -aE '^(motion_route |sun_shadow_apply_frame
|sun_shadow_lane_refusals |sun_shadow_lane_frame )'` per file into a scratch
directory, then aggregated in Python; no log was read whole. Field meanings:
`docs/verification/telemetry.md`, `docs/verification/motion-output.md`;
reason strings: `src/proxy/motion_output.cpp:5033-5205` (`evaluate_draw`),
sun-lane refusal gate: `src/proxy/motion_output.cpp:7589-7609`
(`run_sun_shadow_apply`, `skip="lane"`), refusal-bucket log gate:
`src/proxy/motion_output.cpp:1881-1909` (`publish_sun_lane`, logged only when
`sun_frame_.untracked > 0`).

Coverage: runs 116-175 (60 runs). Runs 120, 121, 127 produced zero
`motion_route`/`sun_shadow_*` rows in this window (no capture frames reached,
likely menu-only starts) and are excluded from the counts below. No object
name/sector field exists on `motion_route` or `object_context` rows (only
`mesh`/`model`/`node` hex ids); object-type hints below are inferred only
from render state (zwrite/blend/atest) and draws-per-frame shape, not a
logged label — this is a real gap in the evidence, stated explicitly per
row group.

## 1. Distinct (vs, ps) pairs with routed=0, by unmatched reason

21 distinct pairs across all reasons.

### `unmatched=unregistered` (shader pair outside the motion/sun-shadow registry)

| vs | ps | runs | frames | draws/frame p50/max | zwrite | blend | atest | render-state class |
| --- | --- | ---: | ---: | --- | --- | --- | --- | --- |
| 5e484a06672e28fb | 0a523f33ac47ae05 | 33 | 1114 | 1/1 | 0 | -1,1 | -1,0 | non-depth (harmless) |
| d5e1c75351ed3f04 | 8360f422de08b5bd | 21 | 470 | 3/5 | 0 | 1 | -1,0 | blended, no zwrite (harmless) |
| 36f98d151fd6b0c6 | 222bee0defcb1852 | 20 | 468 | 1/1 | 0 | -1,1 | -1,0 | non-depth (harmless) |
| ac2319bc3953efc6 | 03a16e5c63daa6e8 | **1 (run174)** | 128 | 1/3 | **0,1 (mixed)** | -1 | -1 | opaque when zwrite=1 — **dangerous** |
| 7b6393fe2d3e1d85 | f7e0b6647a3bfa62 | 1 (run174) | 128 | 4/4 | 0 | -1 | -1 | non-depth (harmless) |
| c78b4c68a87fce74 | 0000000000000000 | 1 (run174) | 32 | 16/16 | **1** | -1 | -1 | opaque, null PS hash — **dangerous** |
| 803ebfd17f79e413 | 652a7c5d1e9909a0 | 1 (run174) | 32 | 6/6 | **1** | -1 | -1 | opaque — **dangerous** |

### `unmatched=no_zwrite` (registered pair, depth test off this draw)

3 pairs, all `zwrite=0`, `blend=1`, harmless class (blended/no depth write):
`494fe349b8bc12ec/7c83ed50c9894e44` (3 runs, 16 frames, 9/13 draws/frame),
`494fe349b8bc12ec/e70adc744a38ca59` (2 runs incl. 174, 34 frames), and
`53a0a641107ed76c/63f96eba9eea7880` (run174 only, 32 frames, 6/6).

### `unmatched=history` (registered pair, one-frame history-lookup miss)

10 pairs, low frame counts (1-11 frames each, 26 total distinct-pair/run
combinations across runs 139,143,144,161,168,169,170,174); all `zwrite=1`,
`blend=0` except one `zwrite=0` — transient, not a coverage gap (the pair
*is* registered).

### `unmatched=overlay_node` (compositor/HUD draw, excluded by design)

1 pair, run174 only, 32 frames, 14 draws/frame, `zwrite=0` — harmless by
construction (overlay path).

## 2. Opaque depth-writing (dangerous) vs harmless

Only **run174** has opaque (`zwrite=1`) unregistered pairs: `ac2319bc.../
03a16e5c...` (mixed zwrite, 128 frames), `c78b4c68.../00000000...` (null PS,
32 frames, 16 draws/frame — a real depth-writing scene primitive with no
pixel shader ever recorded), `803ebfd1.../652a7c5d...` (32 frames, 6
draws/frame). No other run in 116-175 shows an unregistered opaque pair. All
other unregistered/no_zwrite pairs across all 60 runs are `zwrite=0`
(blended/non-depth) and cannot punch holes in the depth/motion targets.

## 3. Per-run sun-shadow apply refusal (`sun_shadow_apply_frame applied=0`)

Refusal reason is exclusively `skip_reason=lane` (`sun_lane_active_==false ||
!sun_frame_.published || !sun_frame_.available`) wherever it occurs; `sun`
skip (0.03-0.1% of frames, runs 129,130,139,142-146,153,154) is unrelated and
negligible.

| run | frames w/ apply row | refused | refused % |
| --- | ---: | ---: | ---: |
| **174** | 19311 | **18585** | **96%** |
| all other 116-175 runs | 3200-40000 each | 0 or 8 (≤0.2%) | ≤0.2% |

`sun_shadow_lane_refusals` (logged only when `untracked>0`) appears **only in
run174**: 18585 lane frames, `untracked=24705 unregistered=24705
signatures=18585`, i.e. every untracked writer in every failing frame is
classed `unregistered` — the three opaque unregistered pairs above are the
direct cause of the lane going `available=0` and `sun_shadow_apply` refusing
almost the entire run. No other run in the scanned window shows a single
`sun_shadow_lane_refusals` line, so shadows were effectively off for
essentially all of run174 (Argon Prime) and the user would see it only as
missing/flat shadowing, not an error.

## 4. Other refusal/fallback counters (runs 135-175, `filtered2/` scan)

Steady baseline present in nearly every run (startup-time, not per-frame
degradation): `bloom_refusal` (1), `hull_emission_refused`/`_state` (16),
`emission_source_gain_refused_state` (16, absent in a handful of runs),
`voice_dmo_fallback` (7, or 11 when `shadow_replay_depth_refused` also fires,
8 times, in runs 139,142-146,153,154,174 — a separate, unrelated cascade
condition, not tied to the unregistered-pair finding). **run174 uniquely**
also logs `motion_output_scene_hook_disagreement=16` and
`voice_dmo_fallback=17` (elevated vs. the 7/11 baseline) and `bloom_refusal=2`
— consistent with run174 being an outlier run with a scene the proxy had not
previously exercised, not just a shadow-lane-specific issue.

## Open issues

- No sector/object-name field exists in these logs; "opaque scene draw"
  classification is state-based only. A precise object identification (ship
  class vs. station part vs. asteroid) needs a new diagnostic: log the
  engine's node type/name (if available at the hook site) or model file path
  alongside `motion_route`'s `model=` id on unregistered rows.
- Runs 120/121/127 were not evaluable (no rows in this window); not covered.
- Older runs (<116) were not scanned; if the brief needs full ~120-run
  coverage, extend `filter_one.sh`/`analyze.py` in
  `/private/tmp/.../scratchpad` (not part of this repo) to that range.
