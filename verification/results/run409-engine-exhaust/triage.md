# Run 123 A triage (run409), numeric only

Log `/tmp/x3-bottleX3-run409/session-20261004-033226-216.log` (580 MB, not tracked), DLL a252c522 from c8f36eb6, bottle X3, 38,084 frames, 781 s, resets 0, exception 0.
Producer: `python3 triage.py <log> > run409.txt` (streams once, ~19 s). All figures measured.

## 1 Presets
18 hotkey changes (`grep '^engine_plumes_preset' <log>`): 11212 strong, 11264 restrained, 11397 default, 14192 strong, 14253 restrained, 14869 default, 14982 strong, 15033 restrained, 15079 default, 15127 strong, 15140 restrained, 15166 default, 15210 strong, 15227 restrained, 16145 default, 16213 strong, 16225 restrained, 16245 default (to end).
F8 capture frames (depth_1_*): 2421-2428, 3253-3260, 5026-5033, 6683-6690, 8284-8291 (5 sets x 8): all default (first change at 11212).
Ran frames per preset: default 34,912, restrained 1,759, strong 206.

## 2 engine_stage (ran=1), median / p95 / max
| field | default | restrained | strong |
|---|---|---|---|
| records | 20/43/70 | 19/27/31 | 18/25/26 |
| nozzles | 14/34/46 | 19/23/26 | 16/25/26 |
| far | 8/22/44 | 8/17/17 | 4/11/11 |
| far_records | 8/29/51 | 6/14/14 | 6/11/11 |
| far_dropped | 0/24/160 | 0/0/0 | 0/0/0 |
| far_overflow, far_disarmed, floor_unknown, culled_behind, culled_idle, skipped_other_view, attacks | 0 max | 0 | 0 |
| floored | 12/32/41 | 17/21/24 | 14/23/24 |
| capped | 0/2/5 | 0/2/2 | 2/2/2 |
| faded | 0/4/7 | 0/2/4 | 2/2/2 |
| culled_small | 1/20/50 | 0/4/4 | 0/4/4 |
| culled_rows | 0/8/20 | 0/8/12 | 0/0/0 |
| discs | 12/26/43 | 15/23/24 | 14/23/23 |
| stage_us | 132.8/281.3/18427 | 116.7/159.1/472.8 | 110.4/142.6/212.1 |
| view_rule | own 34841, far 69, none 2 | own 1759 | own 206 |
Min of every count 0 (default), stage_us min 11.9. stage_us > 2 ms: frame 824 (15.3 ms, arm), 2236, F8 frames 2422-2428 (5-6.4 ms), 2876.
armed=0 reason=pending 821 frames (0..823), armed=1 37,260. SETA: 663 frames seta=1, all with travel>0.

## 3 engine_light_frame (36,817 rows)
ships 6/12/20, ships_drawn 5/12/19, nodes 12/30/37, candidates = draws_lit 24/56/76, records 20/43/70; invalid, orphan, ships_dropped, nodes_dropped, log_dropped, other_view all 0. No lights/evictions/own-ship field in the row. No error/disarm row; engine_light_mode on, ships_max 256; 26 engine_light_variant rows all refused/mismatched/failed=00.

## 4 engine_shimmer (38,081 rows)
drew 1/1/1 (med/p95/max), rects 2/4/4, refused 0/8/20, capped 0/8/11, failed 0, cpu_us 160/266/49476. on=0 55 frames: Ctrl+Alt+F7 toggles at 5616 (off) and 5671 (on). Skipped: no_resolve 1038, no_plumes 226, no_rects 70, off 55.

## 5 engine_seta
11 rows: read ok at 824, 5 engage/release pairs (18972/19071, 25486/25557, 27811/28038, 28167/28313, 33027/33147), warp 1.9, travel ~0.996 at release; refused 0, invalid 0; stage rows seta_read=ok 36,877.

## 6 Frame time (frame_end dt_ms, min/med/p95/max)
ALL 3/18/22/47352 (load at 824; F8 frames ~600 ms); default 3/18/22; restrained 15/18/22/64; strong 16/18/21/25. frame_phases dt_p50 (126 windows) median 18.3 ms. run407/408 (run407-408-engine-plumes/run40N.txt): dt median 17.0, stage_us median 98.5 / 102.7; run409 default stage_us median 132.8.

## 7 Error-type rows
Row types: voice_dmo_fallback 7, bloom_refusal 1 (scene_handoff), bolt_footprint_refused 1 (buffer, frame 5551), chase_view_restore_arm_refused 1, emission_source_gain_refused_state 16, hull_emission_refused 16, hull_emission_refused_state 16, shadow_replay_depth_refused 8 (sun_changing). No device-lost/reset/error/warning row types; session_end resets=0 exception=0. log_writer dropped_total 0 (sample).
launcher-stderr.log (114 lines): 16 GStreamer-CRITICAL assertions (2 bursts, 03:32:34 and 03:45:23), 26 "Some triangles have zero area" warnings at 03:45:27, msync start lines, no Wine err/crash.

## 8 engine_plumes_state
One row (frame 824, armed=1 reason=armed glow=suppressed). No armed=0 state row; engine_stage armed=0 only as pending before 824.
