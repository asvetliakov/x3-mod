# GPU sync timing (`--gpu-sync-timing`)

Design and log format: [engine-frame-time.md, "GPU sync timing"](../architecture/engine-frame-time.md#gpu-sync-timing).

## 2026-09-23: implementation (uncommitted worktree, not installed)

- Host: `PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification.analysis.test_gpu_sync_timing`
  (core arithmetic through `verification/probe/gpu_sync_timing_core_fixture.cpp` in release and
  ASan/UBSan builds, 32 checks; launcher dry run and `--vanilla` refusal; the runner's parse and
  acceptance on synthetic transcripts; the production wiring of all 14 passes).
  8 tests OK (measured). Full host suite (`run_host_suite.py`): 239 modules, 2,450 tests, 0 failing
  (measured; the bloom-lifetime fixture and the fog-card mock gained the off-state helpers).
- Build: all targets from a scratch CMake tree (MinGW i686, RelWithDebInfo) with 0 warnings
  (measured); `verification/probe/check_no_x87.py` on the DLL: PASS, 660 reachable, 0 violations
  (measured). `GpuSyncTiming::detach`/`release` are reachable from the light hooks through the
  device context's destructor; they hold only the FNSAVE/FRSTOR transport the audit allows.
  Fixture executable sha256 `8136127c…8fb8` (build-gst-51513, uncommitted tree).
- Wine fixture: `verification/probe/gpu_sync_timing_fixture.cpp` (CMake target
  `gpu_sync_timing_fixture`; 30 checks: both soft-fail paths, references, CPU state across a
  boundary, 48 frames of fake passes in three 16-frame windows, exact sync count, Reset,
  failed Reset, detach). Runner:
  `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_gpu_sync_timing.py --exe <build>/gpu_sync_timing_fixture.exe`
  writes `verification/results/bottle-X3/gpu-sync-timing.{json,txt}`.
- Not verified: native Windows behaviour; the in-game pass figures (one diagnostic flight).

## 2026-09-23: Wine fixture in bottle X3 (orchestrator run)

`verification/results/bottle-X3/gpu-sync-timing.json` (tracked on main; exe `8136127c…8fb8`,
built from the round-0 tree): passed, 30/30 checks, 15.6 s (measured). Attach 28 queries holding
28 device references; both soft-fail paths refused cleanly; 48 frames in three 16-frame windows
with 1,440 syncs (28 boundaries + the second Bloom pair per frame), 0 issue/data failures, 0
timeouts, 0 dropped frames; Reset released and recreated all 28. Sync floor: empty pair 18 us
median, a pair around one 16x16 quad 264 us median; the fixture's heavy fog pass (96 blended
full-screen quads plus the nested light pass) 5,760 us; serialised dt median 24,064 us.

## 2026-09-23: review round 1

Queries released only at the frame's end under `BloomOperation` after the failure cut-off
(references zeroed before the release loop); four session timeouts latch a cut-off no Reset
lifts; 50 ms spin limit while the previous boundary failed; native `TestCooperativeLevel` at
the frame's `scene` begin abandons a lost frame without a spin; an abandoned frame files no dt;
the fixture's CPU-state check also compares the x87 control word (FNSTCW). The fixture source
changed, so the recorded exe predates it: rebuild before the next run.

## Log columns

| Column | Meaning |
| --- | --- |
| `window` | 300-frame window index (1-based) |
| `pass` | pass name; `none` when a window measured no pass (the row still carries dt) |
| `median_us`, `p90_us` | nearest-rank window figures of the serialised span per frame (CPU submission plus GPU execution, includes about 18 us sync floor per pair and any nested pass; pairs of one pass in a frame add up) |
| `n` | frames of the window that measured the pass |
| `wait_median_us` | the end spin alone: GPU work still pending when the CPU finished submitting |
| `session_n`, `session_median_us`, `session_p90_us` | the session so far, from the histogram (exact below 64 us, <= 6.25 % above) |
| `dt_median_us`, `dt_p90_us` | Present-to-Present CPU interval of the serialised frames in the window (abandoned frames excluded) |
| `frames`, `window_frames` | first..last frame of the window and its frame count |
| `dropped` | frames abandoned by a failed or timed-out sync or a non-cooperative device |
| `unclosed` | passes still open at a frame's end (not filed) |

## Run 274 (Run 73 C, 2026-09-23): first flight, 1920×1080

Diagnostic ran (available=1, 28 queries, 15 windows, dropped 0, no timeouts, no Reset). Per-pass medians and the
reading of them are in [engine-frame-time.md, "Run 274"](../architecture/engine-frame-time.md#run-274-gpu-per-pass-cost-at-19201080-run-73-c-2026-09-23):
proxy passes 11.7 ms serialised vs engine span 4.7 ms in the fogged stand window; fog_route 4.43 and taa 2.90 ms
are the two large ones. Open: the final window and the `gpu_sync_timing_summary` rows were not written at exit;
per-frame rows (or an inside/outside-engine tag on hdr_writeback flushes) are needed to isolate an F8 burst.
Summariser: `verification/results/run274-gpu-sync/gpu_sync_windows.py`.

## 2026-09-24: `taa_*` sub-passes (uncommitted worktree, not installed)

Five passes appended after `present` (indices of the first 14 unchanged): `taa_copy`, `taa_mask`, `taa_box`,
`taa_resolve`, `taa_display`, marked inside `TemporalPass::run` through `configure_sync_timing` (the motion output
sets it before every run; null when the option is off: one branch per boundary, no device call). 19 passes, 38
queries. Each pair adds about 0.26 ms to `taa` and to the serialised frame, so the next flight's `taa` reads about
1.1 ms higher than run274's for the same work (four pairs at the 0.264 ms light-pair floor on the HDR route, where
`taa_display` does not run; inferred). What each
covers: [engine-frame-time.md, "TAA stage cost"](../architecture/engine-frame-time.md#taa-stage-cost-2026-09-24).

- Host: `PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification.analysis.test_gpu_sync_timing`,
  8 tests OK (measured): the core fixture checks 19 passes / 38 boundaries and the new names; the wiring test finds
  one begin and one end site per sub-pass in `temporal_pass.cpp`.
- Wine fixture (`gpu_sync_timing_fixture.cpp`): each frame now nests the five sub-pass pairs inside `taa`; the
  nesting check requires `taa` >= each sub-pass median, the light reference is `taa_resolve`, and the exact sync
  count follows `boundary_count` (48 x (38 + 2)). Check count unchanged (30). Run in bottle X3 (scratch CMake build,
  exe `9ba31650…ac7e`, 8.5 s): PASS 30/30, 38 queries holding 38 device references, 1,920 syncs, 0 failures, 0
  timeouts, 0 dropped frames, Reset recreated all 38; each sub-pass around one 16x16 quad reads 188–190 us window
  median and the `taa` pair nesting all five 1,049 us (measured). The runner's rewritten
  `gpu-sync-timing.{json,txt}` were restored to the committed ones.
- Temporal pass (`TemporalPass` null marks) and motion output unchanged: see "TAA stage cost" for the
  `run_temporal_pass.py` (744 / 278 / 546, report byte-identical) and `run_motion_output.py` (190 cases, 271,369
  checks, 0 differing stable fields) results.

Note (2026-09-23): the committed `verification/results/bottle-X3/temporal-lattice.txt` still carries the pre-cut instruction-slot rows (line_mask 368, line_mask_camera 344, thin_box_rows 35); the post-cut counts 399 / 372 / 77 come from the TAA cost worktree's lattice run and are refreshed by the next full temporal run that is committed.
