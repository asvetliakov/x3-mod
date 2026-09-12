# Fixture runners and CrossOver bottles

Status 2026-09-12 (evening): the runner-side switch is implemented and the
five suites were run once under the X3 bottle (records under
`verification/results/bottle-X3/`, capture logs over 1 MB ignored via
`.gitignore`). Two FEX-specific limitations were found (the sampler's thread
context and the CRT's text formatting of floats); see the record below.

## Two bottles

| Bottle | WineArch | Wine | Environment lines recorded | Game files |
|---|---|---|---|---|
| `Steam` | `win64` | x86_64 Wine under Rosetta | `WINEMSYNC=1` | `drive_c/X3/X3AP.exe`, `d3dx9_37.dll` (same bytes as X3) |
| `X3` | `arm64` | CrossOver Preview native arm64 Wine, FEX x86 emulation | `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1` | same |

The game now runs in `X3` (`tools/manage.py` defaults to it, `X3M_BOTTLE`
overrides). The fixtures keep `Steam` as their default so every record under
`verification/results/` stays comparable with the committed evidence.

## Runner-side selection (`verification/probe/bottle.py`)

- `bottle.BOTTLE = os.environ.get('X3M_FIXTURE_BOTTLE', 'Steam')`; every runner
  passes `'--bottle', bottle.BOTTLE, '--no-update'` (48 scripts: the `run_*.py`
  runners, `mesh_run.py`, `temporal_run.py`, and the three `verify_*.py` readers).
  `wine_args()` returns the same triple for new code.
- `bottle.results_dir(root)`: `verification/results` for Steam, otherwise
  `verification/results/bottle-<name>/` (created on demand). Runners write and
  read back their own records there. Generated *inputs* that are not per bottle
  stay on the shared path (`rigid-motion-pixel-program.json`,
  `shader-profile-registry.json`).
- `bottle.bottle_dir()` / `bottle.game_dir()` replace the hard-coded
  `Bottles/Steam/...` paths (native `d3dx9_37.dll`, `drive_c/windows/syswow64`,
  `dosdevices`), so the native module hashes recorded by a run come from the
  bottle that ran it.
- `bottle.describe()` is stored as `bottle` in every summary JSON the runners
  write: `{name, wine_arch, environment{FEX_X87REDUCEDPRECISION, WINEMSYNC},
  cxbottle_conf}`, read from the bottle's `cxbottle.conf`. `bottle.label()` is
  the text form used by `sampling-profiler.txt`'s header. Records without the
  key predate this change and were all produced in the Steam bottle.
- `run_sampling_profiler.py`'s idle-wait now skips `pgrep -fl` continuation
  lines of multi-line commands (they are not `pid args` rows and raised
  `ValueError` before any fixture started).
- The fixture logic, expectations and `src/` are unchanged.

## X3 validation plan and record

Run one suite at a time with `X3M_FIXTURE_BOTTLE=X3`, every Wine command
wrapped in `verification/probe/wine_lock.py`; results land in
`verification/results/bottle-X3/`. The Steam references are the HEAD records
under `verification/results/` (the working-tree copies of
`temporal-pass-summary.json` and `loading-trace-mesh-summary.json` are mid-edit
by other agents). Runs: 17:53-18:10 on 2026-09-12, while three other agents
rebuilt `build/d3d9.dll` concurrently.

| Order | Suite | What it stresses under FEX/arm64 | X3 result | Steam reference |
|---|---|---|---|---|
| 1 | `run_sampling_profiler.py` | thread suspension, `GetThreadContext`, TEB reads from the sampler thread | **FAIL 17/22**: the five attribution checks (`a_leaf_attribution`, `a_caller_pair`, `b_frame_wait_b`, `c_leaf_attribution`, `c_scan_caller_pair`) fail because every sample is the creation-time context (limitation 1 below); the mechanics pass (1173 ticks, 3092 samples, 0 dropped, 2 suspend failures, 0 context failures, stop bounded, handles/threads released). Tick mean 282 us, max 10744 us, report 2963 us, refresh 10501 us; overhead a 3.9 %, c 3.7 %, main 8.0 %; walls 6.92 s off (first Wine start of the bottle: wineserver + msync bootstrap) / 3.27 s on | pass, 22/22 checks; overhead a 10.5 %, c 5.4 %, main 0.9 %; tick mean 558 us, max 9144 us, 940 ticks, 2536 samples, 0 dropped; walls 4.19 s / 3.43 s |
| 2 | `run_loading_trace.py` | IAT + vtable hooks, x87 `CpuState` under reduced precision | **pass for the committed cases**: loading-trace 85/85, loading-mesh 123/123, admission witness `0,0,0,0,0,0,0,1` as on Steam; every check line identical, the only differing lines are `coverage_begin` and the benchmark. The suite as a whole exits failed on the uncommitted fourth case `mesh-adjacency-cache-off` (2179 checks; `Default policy differs from native on tie evidence`, `nan-position` mismatches=6) exactly as the Steam working-tree record does: the mesh-adjacency agent's in-flight work, not a bottle difference. FEX cost: `hooked_us_per_call` 1.364 vs 0.378 (3.6x; raw 0.0028 vs 0.0035), `MESH_COUNT` inclusive 100 ns ticks op14 2155 vs 54, op21 2630 vs 505 (first-call JIT), op20 729 vs 1189; native adjacency `quad-16` 490 vs 394 us | pass, loading-trace 85 checks, loading-mesh 123 checks (HEAD) |
| 3 | `run_motion_output.py` | route, scene-hook callsite patch on the fixture exe, HDR FP16 MRT on D3DMetal | **65/78 cases pass, suite aborted** at `seam-hdr-exposure` frame 35: the fixture's `EXPOSURE_BLOCKS` text prints the NaN poison block as a finite `1.78e4932` (limitation 2), the runner parses it as `inf` and the `isnan` assert fires. All 65 recorded cases (route, seam, ownership, HDR, ramp, TAA hook) pass their checks; the eight recorded benches, median of 24 `samples_ms`: 1280x768 TAA off 0.325 / on 0.747 ms, 5120x1440 0.688 / 2.468 ms, HDR 1280x768 0.366 / 0.740 ms, HDR 5120x1440 0.852 / 1.948 ms. Per-case `dll_sha256` is `141f372c` for the 46 seam cases and `29b7fd08` for the 19 production cases (the seam DLL is a separate build by design; no DLL changed mid-run, no provenance assertion tripped). The three `hdrexposure` cases cannot pass on X3 until the fixture stops printing non-finite floats with `%g`; the 10 tonemap cases and 4 tonemap benches after the abort: see the partial run below | pass, 78 cases; same benches 0.427 / 0.833, 0.565 / 2.319, 0.408 / 0.807, 0.951 / 1.961 ms |
| 3b | `run_motion_output.py <14 names>` (partial, `motion-output-partial.json`) | the 10 tonemap cases and 4 tonemap benches the abort skipped | **all 10 cases pass with the Steam check counts** (`seam-hdr-tonemap-fault` 59, `shader-absent` 23, four `seam-taa-hdr-tonemap-*` 140 each, `seam-ownership-taa-hdr-tonemap-on` 140, `production-taa-hdr-tonemap-on` 59, `seam-taa-hook-hdr-tonemap-on` 127 with `hook_status=active`, `seam-taa-hdr-tonemap-fault` 132); tonemap benches, median of 24: 1280x768 TAA off 0.719 / on 1.224 ms, 5120x1440 1.386 / 2.166 ms. A partial run is never a suite pass (no cross-case comparisons); its first attempt also died in the `gz_buffer.cpp` build break. So on X3 75 of the 78 cases pass and the 3 `hdrexposure` cases are blocked by limitation 2 | same benches 1.073 / 1.244, 1.464 / 2.273 ms |
| 4 | `run_temporal_pass.py` | SM3 resolve on FP16 targets, device Reset | **pass**, 386 samples, 204 state restorations, 2 generations; the three negative controls exit 1 with identical oracle errors / drift (0.4229/0.4900, 0.4541/0.9117, 0.2866/0.5570). Six `SAMPLE ... 1px horizontal` lines differ only in the 9th significant digit of the printed centroid (`expected=6.870000000` for `6.869999981`; limitation 2), all PASS | pass, 386 samples, 448/204/2 (working tree) |
| 5 | `run_ownership_integration.py` | 26 isolated proxy cases with `d3d9=n,b` | **PASS**, 26/26 cases exit 0, one DLL (`62318808`) for all cases, source tree and binaries unchanged during the run; `verify_ownership_integration.py` (host-side reader, `X3M_FIXTURE_BOTTLE=X3`) PASS, 26 cases. First attempt (18:08) died in `cmake --build build-ownership` on the gz-buffer agent's untracked `src/proxy/gz_buffer.cpp` (`SEEK_SET` undeclared, fixed by that agent at 18:08:33); the rerun is the record. Wall about 70 s including the clean build | PASS, 26 cases |
| - | `check_no_x87.py` | host-only | skipped by design | - |

### FEX-specific limitations found

1. **`GetThreadContext` of a suspended thread returns its creation-time
   context.** In the profiler fixture every sample of all four threads (two
   spinning in the exe, one waiting, main) reports leaf `ntdll.dll+0x4dd1c`,
   which is the `RtlUserThreadStart` export of
   `lib/wine/i386-windows/ntdll.dll`, with `frame rva=0x0` and
   `stack_unknown=0`: `Esp` is inside the TEB stack bounds (at the initial
   stack top, so neither the EBP chain nor the return-address scan finds a
   frame). The game run in
   `../reverse-engineering/loading-profile-bottle-x3.md` saw the sibling form
   (`Eip=0x10000`, usable `Ebp`). In both forms the context is not the live
   guest state, so leaf/frame attribution is void on X3 while tick timing,
   thread discovery, module tables and overhead remain valid. No safe fallback
   exists inside `sampling_profiler.cpp` (the EBP chain needs the live `Ebp`);
   a guest-PC source other than `CONTEXT` is the orchestrator's call. The
   Steam bottle remains the profiler's validation environment.
2. **The CRT prints non-finite floats as finite numbers and rounds the 9th
   digit** (`FEX_X87REDUCEDPRECISION=1`; Wine's msvcrt formats through the
   80-bit long double path). A guest probe (`nan_probe.cpp`, scratch, run under
   both bottles the same minute) printed `quiet_NaN()` as
   `1.78459724e+4932` and `infinity()` as `1.1897315e+4932` on X3 (`nan`,
   `inf` on Steam) while `std::isnan`, `std::isinf`, `fpclassify`, `x != x`,
   `fucompp` (C0=C2=C3=1) and `fucomip` (ZF=PF=CF=1) classify the NaN
   correctly on both bottles: the arithmetic and compares are right, only the
   text channel is wrong. Consequences: the `hdrexposure` hazard blocks
   (+-inf) happen to parse as +-inf again and pass, the NaN poison parses as
   `inf` and fails; `%.9f` output loses the 9th significant digit
   (`temporal-pass.txt`). Fix belongs in the fixtures (print the IEEE bits of
   hazard/poison values), not in the runners' parsers. **Fixed (pre-review
   28):** `EXPOSURE_BLOCKS` prints every non-finite value as its IEEE bits
   (`0x%08x`; finite values keep `%.9g`) and `run_motion_output.py`'s
   `parse_float` decodes that form, so the three `hdrexposure` cases no longer
   depend on the CRT's text channel; production is untouched.
3. **Per-call cost of tiny cross-DLL hooks is 3-4x higher** (loading trace
   1.364 vs 0.378 us per hooked call; consistent with the 3.2x `gzread` finding
   in the game). GPU-bound boundaries (motion-output benches) are within +-25 %
   of Steam either way.

### Other observations

- The concurrent agents' rebuilds of `build/d3d9.dll` did not trip any
  provenance assertion (`run_motion_output.py` and
  `run_ownership_integration.py` build their own DLL at the start and copy it
  per case; `run_loading_trace.py`/`run_temporal_pass.py` hash their own
  fixtures). What did interfere was an uncompilable in-flight source
  (`src/proxy/gz_buffer.cpp`, 18:04-18:08): both runners that rebuild the DLL
  failed at `cmake --build` and were rerun once it compiled. `run_sampling_profiler.py`'s idle wait now also skips
  `wine_lock.py` wrappers that are only waiting for the lock (they execute
  nothing under Wine; without this, other agents' queued runners deadlocked the
  30 minute wait).
- Capture logs over 1 MB under `bottle-X3/` (`motion-output-*-capture.log`,
  6.5 MB each) are ignored through `.gitignore`; the JSON/text records stay.
- The Steam bottle stays the default for the fixtures: on X3 the profiler and
  any fixture that prints non-finite floats cannot be validated.
