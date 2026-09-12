# Review 18: resolve quality pass, route cost telemetry with lazy MRT binding, iteration-8 analyses

Independent review of the uncommitted tree on top of `4c2efc7` (three agents'
work): the temporal resolve quality pass (`src/temporal/resolve.hlsl`,
`resolve.h`, the regenerated `temporal_resolve_program_inc.h`,
`temporal_pass.{h,cpp}`, both temporal fixtures and runners, the design
documents), the route/boundary cost telemetry with the lazy RT1/RT2 binding
experiment (`telemetry.{h,cpp}`, `motion_output.{h,cpp}`, `capture.cpp`,
`summarize_telemetry.py`, the motion-output fixture and runner, `manage.py`,
the documents), and the two read-only iteration-8 deliverables (TAA rerun
analysis, loading analysis and the loading-orchestration disassembly notes).
Out of scope and untouched: the sampling profiler and loading trace files
another agent is editing. Every suite below ran on the final tree after a
fresh `build/` rebuild, one Wine runner at a time; result files were queried
with scripts, never read whole.

## 1. Temporal resolve quality pass

Checked against the claims, by reading `resolve.hlsl` in full and decoding
the embedded bytecode's opcodes with a short script:

- **(a) Constant layout** - `ResolveConstants` is eight registers
  (`static_assert` 8x4 floats), uploaded as one block to c0-c7 and c7 again
  for the mask snapshot (`temporal_pass.cpp:279,289`). `prepare` packs
  `history[2] = weight` (c5.z, read by the final `lerp`), `history[3] =
  valid` (c5.w), `options[3]` = 0 / 1 / 2 for off / current-only /
  camera-far-plane (c7.w, `resolve.h:69`), and `rejection` = {absolute
  tolerance, relative tolerance, HDR limit, minimum W} (c6). `FrameInputs::
  rejection` copies into `constants.rejection` unchanged; its new default
  `{1e-4, 0.02, 65000, 1e-6}` matches `resolve.h`'s. `sentinel_camera` is
  only honored with `DerivedFromDepthSentinel` (`temporal_pass.cpp:246`) and
  the route never sets it, so the route still uploads policy 1 exactly as
  before.
- **(b) No NaN/Inf feedback** - every history tap passes `finiteColor`
  (`all(abs(v) <= limit)`, NaN-false) before it contributes; a tap that does
  not contribute leaves `total` short and `total < 0.5` rejects; the
  surviving history is `clamp`ed to `[low, high]` built only from finite
  current neighbors plus the (cleaned) center, and `low <= mean <= high`
  holds by construction, so the clamp is well defined; the output is
  `lerp(color, clamped, w)` of two finite values within the FP16 range
  (HDR limit 65000 < 65504). The current pixel's own NaN/Inf returns
  current-only (cleaned to 0) before any history read. The one fail-open I
  found is finding 1 below.
- **(c) Catmull-Rom** - `w0+w1+w2+w3 = (-0.5f+f^2-0.5f^3) + (1-2.5f^2+1.5f^3)
  + (0.5f+2f^2-1.5f^3) + (-0.5f^2+0.5f^3) = 1` per axis, so the 16 products
  sum to 1; negative lobes are not clamped, the result is divided by the
  contributing weight sum and then clipped by the neighborhood box, so no
  ringing escapes the clip (the fixture's half-texel case, 0.671875 =
  (-1, 9, 9, -1)/16 over 0.25, 0.25, 1, 0.25, then clipped and blended to
  0.4609375, exercises exactly this). Only `tex2Dlod` is used (59 `texldl`,
  no `texld` in the bytecode).
- **(d) Stationary interior** - the disocclusion threshold is the closest
  valid depth of the current 3x3, which is <= the pixel's own depth, and a
  stationary surface's history depth equals its own depth, so `previous >=
  expected - tolerance` always holds there; only history strictly in front
  by more than `max(1e-4, 0.02 |z|)` rejects. The stationary oracle passes
  with error 0.000488 (one FP16 ulp) on 64 frames, all pixels (below).
- **NaN-safe comparison claim** - in the HLSL every test whose false branch
  must catch a NaN (`finiteColor`, `validDepth`, `maskSafe`, the sentinel
  history test, the disocclusion accumulation) is written with `>=`/`<=`.
  The bytecode as a whole is not "only `>=`/`<=` forms": it holds 267 `cmp`,
  2 `if_ge`, 6 `if_lt` and 22 `if_ne` (the `if_lt` compare `c7.z`/`c7.x`
  option flags and `c10.z`-style constants against temporaries, the
  `if_ne` are the `weight != 0` / `motion.w != 0` / boolean branches). The
  NaN-critical paths are each covered by a fixture case (invalid history
  color, invalid current color, invalid current depth, invalid history
  depth, NaN/Inf mask values), which is the evidence that matters.
- **(e) Cost** - the boundary benchmark is the `bench` mode of the
  motion-output fixture (EVENT-synchronized QPC around the boundary
  `StretchRect`, CPU-inclusive); numbers in the results table.

Verified and left unchanged: the closest-depth 3x3 dilation (the dilated
pixel's velocity applied at this pixel, ties to the center), the
motion-path/camera-path jitter handling from review 17, the sentinel
history tap acceptance, the `[branch] all(f == 0)` single-tap path with
the 1e-4 snap, the fixture oracles for the ten new resolve cases, the
`edge_cases` scenes (thin lines, silhouette, resampling blur with a CPU
Catmull-Rom model), the negative controls, and the documents' numbers
against `temporal-resolve-summary.json` / `temporal-pass-summary.json`.

## 2. Telemetry and lazy MRT binding

- **(a) Restore coverage in lazy mode** - `restore_bindings()` (a two-bool
  early return, `motion_output.cpp:315`) runs before: every draw that does
  not route (`before_draw`), `SetRenderTarget`, `Clear` (`before_clear`),
  `StretchRect` (`before_stretch`), `ColorFill`, `UpdateSurface`,
  `UpdateTexture`, `DrawRectPatch`, `DrawTriPatch`, `CreateStateBlock`,
  `BeginStateBlock`, `EndStateBlock`, state block `Apply`, query `Issue`,
  `EndScene`, `Present` (`before_present`), `Reset` (`before_reset`), the
  final device `Release`, the selector leaving the scene phase
  (`observe`), the sentinel fill, a failed apply, and (installed only in
  lazy mode, `capture.cpp:1096`) `GetRenderTarget` and
  `GetRenderTargetData`; capture-frame draw diagnostics restore first too.
  Slots 38 and 32 are the right vtable indices. The remaining limit,
  `SetRenderState`/`GetRenderState` of `COLORWRITEENABLE1/2` between two
  routed draws, is acceptable for X3: the iteration-8 per-draw session log
  records the bound targets of every captured draw, and all 14,741 draws
  bind `rt0` only (0 `rt1`, 0 `rt2` lines), so the game never uses
  additional targets and those masks are inert for its rendering; the mode
  is an opt-in experiment (`perdraw` default). No hook added; the document
  states the limit.
- **(b) Ordering at the boundary** - the `StretchRect` hook calls
  `before_stretch` before `timer.begin()` and the native copy;
  `before_stretch` restores the bindings first and only then runs the
  resolve (which samples RT1/RT2 and captures the state block), so the
  resolve and the application's copy both see the application's bindings.
- **(c) Hot path with telemetry off** - `stamp()` is
  `telemetry_ ? telemetry::now() : 0` on the per-frame latched bool,
  `record()` checks the same bool, `TemporalPass::run` stamps only with
  `timing_` (set from the same bool each run), and the hooks' `restore_
  bindings()` is the two-bool test. No environment read, no allocation on
  the draw path (`MotionRoute` gained one `uint64_t`).
- **(d) Frame line** - fixed field set appended after `taa_references`,
  written with `vfprintf` (no fixed buffer to overflow), cadence
  `X3M_MOTION_FRAME_LOG` clamped to 1..100000 and defaulting to 60; capture
  frames always log as before.
- **(e) Per-draw mode against the old code** - the apply now issues
  `GetRenderState(COLORWRITEENABLE1)` after the shader/constant sets
  (inside `bind_targets`) instead of before `SetVertexShader`; the set of
  state-changing calls, their order and the undo are unchanged, so the
  device state is identical, and if that getter failed the new path undoes
  the shader/constant sets that the old path had not yet made. `undo` uses
  the counted `bind_target` wrapper over the same native slot. The
  `begin_stateblock` hook, rewritten from the macro, keeps the boundary,
  admission, guard and the `SUCCEEDED`-gated `begin_stateblock()` call.
  Equivalence is also measured: the four lazy regular runs and the burst
  runs are identical to their per-draw twins in color hashes, state
  signatures, RT1/RT2 hashes and readback files.

Telemetry names and the `Metric` enum agree in order (static_assert on the
count); `summarize_telemetry.py` skips frame lines without the cost fields
instead of rejecting older logs.

## 3 and 4. Iteration-8 analyses (read-only deliverables)

Both test modules run (17 TAA tests, 10 loading tests). Twelve numbers of
`iteration-08.md` and eleven of `iteration08-loading.md` were checked
against their JSON files (records, routed/matched counts, gate histogram,
burst cut medians, class ratios, blur/HF ratios, yellow-line metrics,
`frame_normal` timings, the regime split, run-B counts, the boundary
bracket; gap seconds, whole-run totals, texture helper, `gzread`, coverage,
SHA-256s, work vectors) and all matched except finding 3. The analyzers
read only CLI-supplied logs/captures under `/private/tmp/x3-iteration08*`
and write only their `--output`; the JSON files carry no shader bytes (only
16-hex hashes and the two log SHA-256s). `loading-orchestration.md` contains
no decompiler output: no C block, one quoted instruction, `DAT_`/`local_`
names used as anchors in prose. `X3LoadingOrchestration.java` writes only
to `args[0]` and names `/tmp/x3-loading-orch/` in its usage; nothing under
the repository.

## Findings and fixes

1. **Low, fixed** - `resolve.hlsl` sentinel policy test `options.w > 0.5
   && depth < 0`: a NaN current depth passes a `<` test in the compiled
   form (the shader's own comment explains why) and under policy 2 became
   a far-plane pixel that accumulates history instead of failing
   `validDepth`. Policy 1 (the route's) was unaffected (current-only either
   way). Now `depth <= -0.5` (NaN-false; the sentinel is exactly -1, as in
   the history-tap test). Regenerated with the native compiler: 3,794 words
   (3,787), bytecode `95eb92e5...`; `src/temporal/README.md:189`,
   `docs/architecture/temporal-integration.md:435,460` updated.
2. **Low, fixed (process)** - the runners' game guard (`pgrep -ifl
   X3AP.exe` or a regex over `ps`) refused to run while a Ghidra headless
   session (`AnalyzeHeadless ... -process X3AP.exe`) was open. New
   `verification/probe/game_guard.py` (`game_running() -> list[str]`) lists
   only real game processes: a command named `X3AP.exe`/`X3AP`, or a
   `wine`/`wine64`/`wine-preloader`/`wine64-preloader`/`winewrapper.exe`
   process whose first non-option argument (value-taking loader options
   consumed, `--` honored) ends in `X3AP.exe`; lines containing `java`,
   `ghidra`, `AnalyzeHeadless` or `pgrep` are ignored; an unreadable process
   table raises. Unit-tested on synthetic `ps` lines
   (`verification/analysis/test_game_guard.py`, 4 tests) and switched in the
   22 runners `run_application_admission[_abi]`, `run_draw_input`,
   `run_execution_ownership`, `run_finite_upload`, `run_geometry_lease[_
   benchmark]`, `run_hook_admission_benchmark`, `run_managed_upload_
   {contract,performance}`, `run_material_motion`, `run_motion_capture`,
   `run_mesh_cache_hook`, `run_mesh_adjacency_cache`, `run_motion_output`,
   `run_ownership[_admission,_integration]`, `run_process_admission`,
   `run_rigid_{motion,replay,retirement}` and in
   `tools/shaders/generate_rigid_motion_pixel.py`, each keeping its refusal
   message and exception type (`run_execution_ownership` keeps its
   "cannot establish game absence" branch). `run_loading_trace.py` is out of
   scope and unchanged.
3. **Documentation, fixed** - `docs/verification/iteration-08.md:213`
   claimed the routed-interior variance "cut by a factor of 125-140 (ratio
   0.007-0.008)"; the JSON `routed_interior.aggregate_ratio` is 0.007515
   and 0.006607 (133x and 151x). Restated.

4. **Low, fixed (verification chain)** -
   `verification/probe/run_ownership_integration_fallback.py:19` lists the
   production objects `build-ownership/` must contain; the concurrent
   `CMakeLists.txt` change adds `src/proxy/sampling_profiler.cpp`, so the
   fallback suite refused to run ("Build all current production components
   first"). Added `sampling_profiler` to that set (the profiler agent's
   component; the ownership manifest hashes this runner, so the integration
   suite was rerun before the fallback). Related: the three shader
   manifests record the generator script's own hash under `tool_sources`;
   after finding 2 changed the script, `current_depth` and `rigid_motion`
   were regenerated (headers and bytecode identical, manifests updated) so
   `--check` passes for all three.

Observations, not changed:

- The bytecode-level "only `>=`/`<=`" wording in the shader comment and the
  documents describes the NaN-catching tests, not the whole program (see
  section 1); the fixture coverage of those tests is what proves it.
- A `GetRenderState(COLORWRITEENABLE1/2)` by the application while a lazy
  binding is held would return 15; same evidence as (a), same verdict.
- `iteration-08.md` also cites the DLL SHA-256 and the iteration-7 column
  from the earlier summary; those are not in `iteration-08-taa-summary.json`
  by design (log-derived / previous iteration), not errors.

## Results after the fixes

`build/` (RelWithDebInfo, `--clean-first`, by `run_motion_output.py`) and
`build-ownership/` (Release, `--clean-first`, by `run_ownership_integration.py`)
were rebuilt on the final tree. Mid-chain another agent changed
`CMakeLists.txt`, `src/proxy/capture.cpp` and `src/proxy/loading_trace.*`
(its sampling profiler, out of scope); the suites that hash those files were
rerun after the tree had been quiet for seven minutes, and every result
below is from that final tree (the temporal suites and the scene capture
hash none of the changed files and were not repeated).

| Suite | Result |
| --- | --- |
| `run_motion_output.py` | PASS: 34 runs (30 cases + 4 bench), all 30 cases exit 0; production TAA 69 checks / 51 restorations, seam TAA 150 / 51, plain, through the wrapper and in lazy RT mode; lazy equivalence: the four lazy regular runs equal their per-draw twins (color hashes, 16 readback files each, checks, restorations), the burst runs identical in color, state signature, RT1/RT2 hashes and readback files, `set_rt` 20 per frame per-draw against 12 lazy (20 in the capture frames 7-8); sources unchanged during the run |
| `run_ownership_integration.py` | PASS: 26 cases, all exit 0 |
| `run_ownership_integration_fallback.py` | PASS: 24 production objects, fault `wrap_factory E_OUTOFMEMORY without consuming native ref`, binaries unchanged (after finding 4) |
| `run_temporal_pass.py` | PASS: 318 numerical / 164 state comparisons, 292 samples, 2 generations; stationary scene oracle error 0.000488, interior delta 0, edge delta 0.0825, ramp delta 0.0044, edge DC error 0.0040, corner DC error 0.0128, analytic error 0.0551, ramp DC 0.0064, drift 0 px, ramp gradient energy ratio 0.9947; negative controls all exit 1 on the one-step oracle: previous-jitter 0.4229, flipped-sign 0.4541, no-jitter 0.2866 |
| `temporal_run.py` | PASS: 78/78 samples, 2 generations, RESET PASS |
| `run_scene_capture.py` | PASS: 36 scenarios, 4,908 checks, 16 samples, fresh build, executable unchanged during the run |
| `generate_rigid_motion_pixel.py --check` | PASS: `current_depth` 35 words `d097c156...`, `rigid_motion` 176 words `a604ce8c...`, `temporal_resolve` 3,794 words `95eb92e5...`, each equal to the embedded header and manifest; only our authored shaders, no game bytes |
| `check_no_x87.py build/d3d9.dll` | PASS: 125 reachable functions, 0 violations (on the final DLL) |
| `python3 -m unittest discover -s verification/analysis` | 503 tests OK on the final tree (495 before the profiler agent added its tests; includes the 4 `test_game_guard` tests, the 17 iteration-8 TAA and 10 loading tests, and the telemetry summary tests) |

Boundary benchmark (motion-output fixture `bench`, median of 20 timed
frames, CPU-inclusive, EVENT-synchronized): 1280x768 off / on 0.362 / 0.683 ms (resolve 0.320 ms); 5120x1440 off / on 0.617 / 2.239 ms (resolve 1.623 ms). At `4c2efc7` (1,695-word program, same machine and method) the medians were 0.336 / 1.008 ms (resolve 0.672) and 0.609 / 2.680 ms (resolve 2.071); an earlier run of this review on the same 3,794-word program gave 0.353 / 0.708 and 0.589 / 2.249. The doubled instruction count is therefore not visible in the boundary cost at either size; the measure includes the backend's submission and completion wait, not GPU time in isolation, so run-to-run variance of a few tenths of a millisecond is normal.

Final `build/d3d9.dll` SHA-256: `599352d2d3ea658a812ae40a85ae974bce9ef527bb93729b11c235ba93a68d64`.

Verdict: go for the checkpoint commit of the resolve quality pass, the
telemetry and the lazy-binding experiment (default `perdraw`), and the two
analyses. The next gameplay evidence to collect is the one the resolve pass
was made for: whether edges and thin lines stop trembling with the
one-sided depth test and the variance clip, and the per-frame
`motion_output_frame` cost fields with `X3M_TELEMETRY=1` in both RT modes.
