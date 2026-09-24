# Run 78 A triage (run295-298), DLL d4ba9f05 (ee3bbf88), 5120x1440, bottle X3

All figures measured from the session logs/captures with the scripts beside this file (commands.txt), unless marked inferred.

1. Dither. Logs: run295 `hdr_tonemap dither=1 dither_reason=ok dither_shader=00000000`, run296 `dither=0 dither_reason=off`;
   tonemap agx, exposure auto on both. The hdr_1 bursts are pre-tonemap, so the 8-bit output is modelled (as run291-293-rings).
   Sky, modelled 8-bit G: dither off plateau 96.3-96.8 % / run90 40-47 px (run295 frames) and 91.4 % / 20-21 px (run296);
   dither on 49.9-50.7 % / 4 px. Low-frequency banding |box33(code)-box33(c)| p99: off 0.12-0.27 codes, on 0.021-0.026 codes
   (about 10x lower). Timing (median of 300-frame windows, full-fog windows): run295 on 18.91/21.19 ms, run296 off 18.98/21.31,
   run291 19.68/22.38, run294 19.35/20.49 (the brief's 19.3/20.4 is run294's figure). Dither cost is inside the noise (inferred).
2. Exit: no fault line in any stderr; engine_memory_read_refused 0, device_destroy 0 in all four (and run287). Logs end with
   the destructor summaries (shadow_retention_summary final=1, motion_output_mip_bias_summary, hull_lightmap_widen_summary).
   The refused row is logged only on the final Release that reaches refs 0 through the hook (src/proxy/capture.cpp:1065-1068);
   that path never ran (inferred: the game exits without the final device Release through our vtable).
3. Slot 06: see overlay_bursts_out.txt. In run297 no slot-06 body drew its merged record in any of the 4 bursts; every
   slot-06 census row is lod 0 and every slot-06 draw is lod 0, although the in-memory ladder equals the 06 marker ladder
   (so the 06 record was loaded) and s is far below T_pad (SPP XL parts: s/T_pad 0.31-0.79 near, 0.17-0.36 farther).
   Slot-05 bodies in the same bursts switched (terran_catapult_gate s/T_pad 0.33-0.58, atmolifter, atf_m6m). run287: every
   slot-05 overlay row below s/T_pad 0.7 took lod 1. Not a size, not a refusal (all 3 SPP bodies are in 06), no archive error
   (reader fallbacks only not_gzip; dat pool errors 0). Cause open.
4. TAA: taps row requested=5 drawn=5 reason=ok in all four; no TAA refusal row; gpu_sync off everywhere (no stage timings);
   frame timing run298 22.41/25.49 ms, run297 16.98/17.88 ms (all windows but the first; dt_windows_out.txt).
5. Bolts run298: 7382 bolt draws, 6571 written, failures 0, refused_buffer 3, refused_shape 84 (two windows, frames 9900-10499;
   run277/279: 0). No capture burst in run298.
6. abnormal_rows_out.txt: row sets match the run277/run287 pattern (scene_hook_disagreement 16, bloom_refusal 2,
   shadow_replay_depth_refused 8 sun_changing); nothing new besides bolt refused_shape.
