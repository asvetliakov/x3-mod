# Run 79 A triage (run299-303), DLL d3683ced (df01f23b), 5120x1440, bottle X3

All figures measured from the session logs with the scripts beside this file (commands.txt), unless marked inferred.
Sessions: run299 hold on (3 F8 bursts 5885, 7308, 8417); run300 hold off; run301 aborted launch (104 frames, 13.6 s,
gpu_sync=1 hold off, no save loaded: no motion_output_taa row, 0 TAA frames, clean exit); run302 hold on + gpu sync; run303 hold off + gpu sync.

1. A' rows: `motion_output_taa ... region_hold=1 ps30_slots=512` (run299, run302) / `region_hold=0 ps30_slots=512` (run300, run303);
   `motion_output_taa_history_taps requested=5 drawn=5 bilinear=1 reason=ok region_hold=1|0`. No TAA refusal row, no
   motion_output_taa_masks row. No RESOLVE_BUDGET row and no mask-target count row exist: `line_mask_targets()`
   (src/renderer/temporal_pass.h:413) has no caller. Indirect: taa_mask_x / taa_mask_y gpu-sync passes absent in run302, present in run303.
2. GPU (median over windows of window median/p90, us; rest = 1 window each, pan = run302 10 / run303 5 windows):
   | stage | run302 hold on rest | on pan | run303 hold off rest | off pan |
   | taa_mask | 1542/1557 | 1544/1564 | 2926/2958 | 2925/2969 |
   | taa_mask_x / _y | absent | absent | 674 / 664 | 674 / 664 |
   | taa_box | 2337/2359 | 2341/2417 | 2106/2224 | 2107/2225 |
   | taa_resolve | 2550/2566 | 2563/2679 | 2450/2565 | 2551/2569 |
   | taa total | 6586/6698 | 6667/6820 | 7724/7867 | 7754/7899 |
   Hold on: mask -1383 us, box +231 us at rest and under pan alike, total -1087..-1138 us. The box does not grow under a pan in either mode.
3. terran_station_lod site=0047d01c status=patched reason=ok mode=size setting=size write=atomic in all five sessions;
   no session used `distance`. run299 bursts: every slot-06 body flag31=1; lod=1 (MERGED) on every body with s/T_pad <= 0.99,
   lod=0 at 1.05-1.12 (usc_dock_e_tower) and on usc_small_station_d's near instance; all s/T_pad buckets 0.1-0.9 lod 1, >= 1.0 lod 0.
   Which body is the Orbital Defence Station is not named in the log (see overlay_bursts_run299_out.txt).
4. | run | hold | gpu_sync | dt p50/p95 ms (windows excl first) | TAA span |
   | 298 | - | 0 | 22.41/25.49 | CPU 414 us |
   | 299 | on | 0 | 20.19/21.39 | CPU 213 us |
   | 300 | off | 0 | 17.77/18.64 | CPU 212 us |
   | 302 | on | 1 | 29.15/33.25 | GPU 6.6-6.7 ms |
   | 303 | off | 1 | 30.72/35.06 | GPU 7.7 ms |
   Different flights, so run299/run300 dt differences are scene-dependent (inferred).
5. Abnormal rows match the pattern: scene_hook_disagreement 16, bloom_refusal 2, shadow_replay_depth_refused 8 reason=state
   (run299/302/303). run300: one screen_emission_additive_refused reason=state (frame 7045). Exit: no fault line, engine_memory_read_refused 0.
6. Bolts: run300 136 draws, 135 written, refused_buffer 1, failures 0; other sessions 0 bolt draws.
7. New vs run287/297/298: gpu_sync_timing / volumetric_fog_repair_census (gpu-sync sessions), chase_native_timing_slow
   (run300 distance_producer 10 ms, run302 central_hud 28 ms), hull_emission_draw (burst rows), terran_station_lod.
