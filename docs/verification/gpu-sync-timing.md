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
  writes `verification/results/bottle-X3/gpu-sync-timing.{json,txt}`. Not yet run.
- Not verified: native Windows behaviour; the in-game pass figures (one diagnostic flight).
