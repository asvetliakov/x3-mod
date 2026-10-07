# Frame cost of the mod (modded vs vanilla frame time)

Ledger for "what does the mod cost per frame". Scripts, outputs and producing commands:
[`verification/results/frame-cost/`](../../verification/results/frame-cost/) (`commands.txt`). Session logs are local
(`/tmp/x3-bottleX3-run330..349`). [M] measured, [I] inferred.

## 2026-09-27: assembled from existing logs; no vanilla-vs-modded measurement exists

Bottle X3 (CrossOver Preview, arm64 Wine + FEX). No session in `/tmp` flew vanilla or an all-off DLL, so every
vanilla figure below is inferred. Default-flight sessions at 5120x1440 without `--gpu-sync-timing`: run338 (759c2ac7,
`--debug --perf`, no `--taa-debug`), run339 (same build), run346/348/349 (92f8223d / 2a75e2c3, with `--taa-debug`;
capture frames excluded). dt is taken from consecutive `frame_end` QPC, not the integer `dt_ms` field. States: menu =
`volumetric_fog_sector` reason `no_cockpit` (the draws >= 500 plateau, [draw-calls.md](draw-calls.md)); flight = a
`shadow_replay_depth` row on the frame; split by background index.

### 1. Modded frame time at 5120x1440 [M]

| session / state | frames | p10 | p50 | p90 | p99 | fps at p50 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| run338 flight bg21 (greenvoid stand, draws p50 238) | 5,166 | 17.39 | 20.45 | 25.10 | 27.66 | 48.9 |
| run338 flight bg14 (fogged) | 5,741 | 16.53 | 17.38 | 19.14 | 22.33 | 57.5 |
| run338 flight bg67 (~106-draw class) | 8,467 | 13.85 | 14.71 | 16.32 | 18.32 | 68.0 |
| run339 flight bg21 / bg67 | 4,959 / 5,140 | 18.39 / 13.84 | 23.01 / 14.70 | 26.01 / 16.30 | 29.00 / 19.13 | 43.5 / 68.0 |
| run348 / run349 flight bg21 (Release 0.5.0 build) | 8,525 / 3,425 | 17.82 / 17.84 | 19.95 / 20.83 | 23.84 / 24.23 | 27.49 / 28.51 | 50.1 / 48.0 |
| menu plateau (draws >= 500), run338/339/348/349 | 80-677 | 16.3-16.8 | 17.0-17.4 | 18.1-18.9 | 23.5-32.8 | 57-59 |
| menu below 500 draws (loading, transitions) | 224-530 | 15.4-16.0 | 16.2-16.6 | 18.6-20.6 | 168-205 | |

The `--perf` overlay writes no rows (only `fps_overlay_mode requested=1 refresh_ms=250 window_ms=1000`); it shows a
~1 s rolling mean of the same counter, so it reads below the p50 fps when hitches occur (flight mean dt 20.0-26.8 ms).
The user's 50-60 fps report matches bg14/bg21 p50 (48-58 fps); bg67 runs at 68 fps. Logging cost of `--debug --perf`:
about 34 us per frame of row formatting on the render thread [I, from the per-row fixture cost in
[logging-tiers.md](../architecture/logging-tiers.md), "Writer thread"]; the `--perf` phase stamps and telemetry QPC
reads are not separately measured.

### 2. The mod's own cost per frame at 5120x1440

GPU spans come from the last per-pass attribution flight at this size, run325 (Run83 DLL, `--gpu-sync-timing`,
session medians, `verification/results/run325-run83a-gpu/gpu_passes_325_out.txt`). gpu-sync serialises the GPU: its
dt median was 33.7 ms against 17-20 ms unserialised, and each span carries a sync-pair floor of about 0.26 ms
([gpu-sync-timing.md](gpu-sync-timing.md)), so the serialised sum over-counts the pipelined cost. CPU spans are the
run338/run348 per-frame rows without gpu-sync (`pass_cpu_us_*.txt`, `window-summary.txt`).

| pass | 5120x1440 GPU, ms | CPU submission, ms | source |
| --- | ---: | ---: | --- |
| TAA (copy + half box + folded resolve) | 3.90 [M serialised] (box 0.85, resolve 2.88) | 0.62-0.75 (`taa_run_us`) [M] | run325; run338/348 |
| bloom (two pairs) | 2.62 [M] | in scene hook | run325 |
| HDR write-back + AgX/RCAS (meter nested 0.53) | 2.02 [M] | 0.12-0.15 [M] | run325; run338 |
| HDR exposure readback | 0.12 [M] | 1.4-3.3 (`readback_transfer_lock_us`) [M], a wait on the GPU [I] | run325; run338/348 |
| sun shadow apply | 1.57 [M] | 0.06-0.08 [M] | run325 |
| cascade replay depth | 1.25 [M] (5 cascades since; larger now [I]) | 0.28 (bg67) / 0.72-0.78 (bg21) [M] | run325; run338/348 |
| caster retention | 0.08 [M] | 0.07 (bg67) / 0.23-0.27 (bg21) [M] | run325; run338/348 |
| volumetric fog (fogged sectors only; march 2.0, composite 1.1, repair 1.2, motes 0.2) | 6.53 [M] (7.21 in Run 77 C) | ~0.05 fill [M] | run325; [fog-gpu-cost.md](../architecture/fog-gpu-cost.md) |
| sun occlusion probe (32 taps, one small disc) | < 0.1 [I] | not logged | [sun-occlusion.md](sun-occlusion.md) fixture |
| bolt far composite | 0.006-0.11 [M, fixture FOLD_TIMING, single run] | 0 | commit 2a75e2c3 |
| LOD overlay, cull-small-parts, collide/resource/crypt services | none on GPU (baked data / engine CPU); net CPU effect unmeasured, likely a saving [I] | | |
| motion-output route: draw-hook overhead (`draw_p50 - draw_native_p50`) | | 0.73 (120 draws, 6.3 us/draw) / 1.39 (190 draws, 7.5 us/draw) [M]; menu 0.43 (0.8 us/draw) | window-summary.txt |
| EndScene hook incl. post-chain submission (`scene_p50_us`) | | 2.36 (run338 flight) / 3.15 (run348) vs 0.18 in the menu [M] | window-summary.txt |

Sums: GPU, non-fog passes 11.6 ms serialised [M]; net of pair floors about 8.5-11.6 ms [I]; +5.5-6.5 ms in a fogged
sector [I]. CPU on the render thread: about 3.2 ms (bg67) to 4.6 ms (bg21) of hook, scene and pass submission, plus
the 1.4-3.3 ms readback wait [M]. The AGENTS.md "about 1 us per slot per frame" rule puts the 1,017-slot resolve at
about 1 ms [I]; the measured resolve span is 2.88 ms, so that rule under-reads the resolve.

### 3. 1920x1080

Only one 1080p session exists in `/tmp`: run330 (46cd4f9d, menu only, 3,663 plateau frames, no flight). Menu p50
16.24 ms at 1920x1080 vs 17.27 ms at 5120x1440 (run338) [M]; in the menu the post chain does not run
(`hdr_frame writeback_us`/`motion_output_frame taa_run_us` 0 at both sizes) and the hook overhead is 0.42-0.44 ms, so
the engine's own rendering adds only ~1.0 ms for 3.56x the pixels there [M] (CPU-bound menu, [I]). Flight at 1080p:
run270 (Run72 DLL, 2026-09-23, busy stand, 421 draws) dt 22 ms, CPU-bound, proxy CPU 1.49 ms [M, older build,
[engine-frame-time.md](../architecture/engine-frame-time.md) "Run 270"]; run274 per-pass GPU at 1080p: bloom 1.24,
HDR write-back 1.08, shadow depth 0.87, sun apply 0.74, HDR readback 0.40, TAA 2.90, fog route 4.43 ms [M,
serialised, older TAA and fog]. Measured 5120/1080 ratios for unchanged passes are 1.9-2.1x (bloom, write-back, sun
apply), not the 3.56x pixel ratio, so scaling by 0.28x would under-estimate the 1080p GPU cost. 1080p GPU estimate
for today's chain: TAA ~1.9-2.0, bloom 1.24, write-back 1.08, sun apply 0.74, shadow 0.9, readback 0.4, about
6.3 ms serialised non-fog (~4.5-6.3 net) and ~3.3 ms fog [I]. Not pixel-bound: cascade replay (per caster), retention
walk, route hook overhead, scene-hook submission, the readback lock's CPU side.

### 4. Approximate answer

| resolution, scene | vanilla frame time | modded p50 | modded fps | mod's share |
| --- | --- | --- | --- | --- |
| 5120x1440, greenvoid stand (bg21, ~230 draws) | ~14-16 ms [I] (65-70 fps) | 20.0-20.9 ms [M] (23.0 run339) | 48-50 [M] | ~5-6.5 ms, 25-30 % [I] |
| 5120x1440, light sector (bg67, ~106 draws) | ~9-12 ms [I] | 14.7 ms [M] | 68 [M] | ~3-5.5 ms [I] |
| 5120x1440, fogged (bg14) | unknown | 17.4 ms [M] | 57.5 [M] | fog adds 5.5-6.5 ms GPU when GPU-bound [I] |
| 1920x1080, greenvoid stand | ~14-16 ms [I] (same CPU floor) | ~18-19 ms [I] (no flight measured) | ~53-56 [I] | ~4.5-5.5 ms, mostly CPU [I] |
| 1920x1080 / 5120x1440 main menu (post chain off) | ≈ modded minus ~0.4 ms hook [I] | 16.2 / 17.3 ms [M] | 62 / 58 [M] | ~0.4 ms [M hook overhead] |

Reasoning for the inferred rows: the 5120x1440 flight frame shows no Present wait (present p50 6-13 us) but a
1.4-3.3 ms lock wait on the HDR readback each frame, largest in the lightest sector, so it sits near the CPU/GPU balance;
vanilla drops the ~3.2-4.6 ms CPU share and the ~8.5-11.6 ms GPU share, and its frame is the larger of what remains.
At 1080p the GPU share roughly halves, so the frame falls to the CPU floor and the hit is the CPU share. Caveats:
Wine/wined3d over FEX on Apple silicon (the submission path costs ~4.8 us per draw, [gpu-sync-timing.md](gpu-sync-timing.md)
backend A/B), so native Windows costs differ; sector content moves the frame by 6 ms between bg67 and bg21; the
1080p flight rows are an older build; the mod's CPU savers (cull-small-parts, collide memo, resource reader) and the
LOD overlay change the engine's own time and are not separated; presentation interval 1 (vsync requested) in run338 and run346, yet flight reaches 68 fps, so no 60 Hz cap is visible.

### 5. The measurement that settles it (one launch per resolution, no vanilla launch)

Same stand (the Run 91/92 greenvoid save, bg21), same route (30 s at rest, 30 s pan), at 5120x1440 and at
1920x1080 (the resolution is set in the game menu before the launch), two launches each:

- Features off, DLL forwarding with `--perf` only (dry run exit 0, `dryrun-all-off.txt`):
  `python3 tools/manage.py launch --bottle X3 --perf --no-motion-output --no-taa --no-hdr --no-hdr-bloom --no-hdr-tonemap --no-shadow-replay-depth --no-sun-shadow-apply --no-sun-shadow-lane --no-shadow-cascades --no-shadow-caster-retention --no-sun-occlusion --no-volumetric-fog --no-screen-emission-additive --no-ownership --no-object-trace --no-object-lifetime --camera vanilla --fov game --lod-occlusion off --terran-station-lod distance --cull-small-parts 0 --bolt-footprint 0 --mesh-adjacency native --resource-read native --media-cue-cache off --no-crypt-cache --no-gz-buffer --no-dat-handles --no-collide-box-cull --no-collide-memo --no-collide-sat-sse2 --no-music-keep --no-pause-key-only --no-chase-view-restore --sun-flare-fix off`
  (the launcher prints "default on not sent" for `--taa-thin-vote` and `--fade-rt2-owner`; the installed LOD overlay
  and fog-family data still load, so the baseline is "forwarding + overlay", not byte-vanilla).
- Default flight: `python3 tools/manage.py launch --bottle X3 --perf` (dry run exit 0, `dryrun-default-perf.txt`).

Read `frame_dt.py` p50/p90 per background from each session; the difference is the mod's cost per resolution.
`--perf` rows are the same in both, so the logging cost cancels. Four launches in one sitting; the chase camera is off in
the baseline (`--camera vanilla`), so fly both launches in the same view (first person) or the chase camera's own
cost stays inside the difference.

## HDR readback lock: GPU wait, not a regression (triage 2026-09-27)

(2026-10-08: the readback is now double-buffered and `readback_transfer_lock_us` is split into `readback_copy_us`
and `readback_lock_us`, with `meter_event_ready`; see `docs/verification/hdr-scene-path.md`. The figures below
are the old combined span.)

**What the span is.** `readback_transfer_lock_us` brackets only `GetRenderTargetData(chain_ring_[slot] ->
chain_readback_[slot])` plus `LockRect` of the system-memory tile image, inside `HdrPass::begin_frame`
(src/renderer/hdr_pass.cpp:1441-1451), called from the HDR redirect latch `MotionOutput::begin_redirect`
(src/proxy/motion_output.cpp:9127), i.e. at the start of frame N+1. It is the auto-exposure meter readback (80x23
tiles at 5120x1440), not the capture path. The tile image was drawn by frame N's meter chain inside the write-back,
the last post pass (hdr_pass.cpp:721-770); the copy is issued at the latch, not at the meter, so latency is exactly
one frame (two-slot ring, slot toggled at the latch). The design assumption (hdr-scene-path.md, "waits on work
submitted a Present earlier") holds only while frame N's GPU work is done by N+1's latch. Writer thread, logging and
`--gpu-sync-timing` are outside the span (0 gpu_sync rows, `timing=cpu_qpc` in run338/348) [M].

**Distribution** (`verification/results/frame-cost/readback_lock.py`, output `readback-lock-run{338,346,348,349}.txt`) [M]:

| run, state | frames | lock p10/p50/p90 ms | dt p50 | dt - lock p50 | draws p50 | r(lock,dt) | r(lock,draws) |
| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 338 bg14 (fogged) | 5,741 | 5.00 / 6.34 / 7.70 | 17.38 | 11.20 | 120 | -0.05 | -0.20 |
| 338 bg67 (light) | 8,467 | 1.87 / 3.25 / 4.35 | 14.71 | 11.59 | 99 | -0.03 | -0.20 |
| 338 bg21 (greenvoid) | 5,166 | 0.07 / 1.81 / 2.78 | 20.45 | 18.69 | 238 | -0.02 | -0.09 |
| 348 bg21 | 8,525 | 0.08 / 1.42 / 2.52 | 19.95 | 18.71 | 192 | -0.01 | -0.07 |
| 349 bg21 | 3,425 | 0.07 / 1.14 / 2.61 | 20.83 | 19.34 | 232 | -0.02 | 0.06 |
| menu (all runs) | 304-1,036 | 0 (post chain off) | 16.6-17.2 | | 496 | | |

By draw bin (run338 flight) the lock falls from 3.95-4.05 ms at 50-149 draws to 0.65 ms at 300-349 draws [M]. The
lock is largest where the GPU has most work relative to the CPU (the fogged sector's 6.3 ms matches the 6.5 ms fog
GPU cost) and is uncorrelated with dt: it absorbs the slack between the CPU frame and the GPU frame, and Present
never waits (p50 7-11 us) because this lock already did [M rows, I interpretation].

**History.** The copy+lock placement and the one-frame latency are identical in 1d36c29e (2026-09-12, introduced),
e8970116, 976307f2 (2026-09-20, the three-bucket timer, Run52's build) and 2a75e2c3 (now); later commits touching
hdr_pass.cpp (3d9e4145 dither, da84d232 removal of `comparison_exposure`, ac88d55f format, d15ecf1b/2a75e2c3 bolts)
do not change the ring, latch or lock [M, `git show <c>:src/renderer/hdr_pass.cpp | grep`]. Run52's 154.9 us
(40 sparse rows, run187, 478 draws, CPU-bound ~20 ms) predates the first 5120x1440 flight (2026-09-24), so it was
measured at a smaller target with a lighter post chain [I from the handoff archive]. The 300-349-draw bin here
(0.43-0.71 ms) and the bg21 p10 (70-80 us) are consistent with that figure. No regression commit.

**Conclusion.** A GPU wait (wined3d drains its command stream and the download waits for frame N's post chain) made
visible by the move to 5120x1440 and the heavier post chain, not a latency regression and not a measurement
artefact [I; the wined3d CS/map sync is not read from source]. Cost to the player: the lock serialises CPU and GPU
once per frame, so frame N's post-chain tail cannot overlap frame N+1's CPU work. In the CPU-bound greenvoid stand
the whole lock is recoverable at most: 1.1-1.8 ms p50 of 20-21 ms (5-9 %) [I upper bound]. In GPU-bound sectors
(bg67, bg14) only the GPU idle time inside the frame is recoverable, somewhere between 0 and the lock (3.3 / 6.3 ms)
[unmeasured]. Deciding it needs one launch recording, per frame, a D3DQUERYTYPE_EVENT issued after the meter chain
and polled with GetData(flags 0) at the latch before the copy (done or not), plus the lock span, in bg21 and bg67.
