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
