# Review 24: FP16 HDR scene path, stage 3 (TAA on HDR)

Independent review of the stage-3 tree, started as the uncommitted work on
top of `1d36c29`; while the review was in progress another agent's
checkpoint (`4e1aa20`, `a8d4309`, 17:22–17:23) committed the whole tree,
stage 3 and the fixes of findings 1–4 below included, so the reviewed
state is `a8d4309` plus this file and the rerun records. Nothing was
committed by this review. Limited to stage 3 of the HDR scene path: the FP16 input path and the c22 upload in
`src/renderer/temporal_pass.{h,cpp}`, the luminance weighting in
`src/temporal/resolve.hlsl` / `resolve.h` and its regenerated program
(`temporal_resolve_program{,_inc}.h`, manifest, the generator's 32 KB
bound), the write-back source in `src/renderer/hdr_pass.{h,cpp}`, the
resolve ordering, `hdr_resolved_`, the failure path and `X3M_TAA_K` in
`src/proxy/motion_output.{h,cpp}` and `capture.cpp`, `manage.py --taa-k`,
the fixtures (`temporal_pass_fixture.cpp` `hdr_cases`,
`motion_output_fixture.cpp` case 6 and `HdrFault::Resolve`), their runners
and the documents. Other uncommitted work in the tree (loading profile,
mesh adjacency, `tools/analysis/*`) belongs to other agents and was not
read. No game was launched; one Wine runner at a time (`game_guard` and
`pgrep -fl 'verification/probe'` before every launch, waiting through the
user's game sessions and the mesh-adjacency agent's builds); result files
were queried with scripts, never read whole.

## Checklist

1. **k = 0 identity** - With `X3M_HDR` unset nothing on the 8-bit path
   changed except the constant upload: `resolve(main, nullptr)` at both
   scene ends (`motion_output.cpp:657`, `:733`) takes the `color_surface`
   input as before, `FrameInputs::luminance_k` defaults to 0
   (`temporal_pass.h:50`), `prepare` writes c22 = (0,0,0,0)
   (`resolve.h:84`) and the pass uploads c0..c7 as one block plus c22
   (`temporal_pass.cpp:280-281`). In the compiled program every weighting
   is `dp3 luma; max(luma, 0); mad r = k·luma + 1.0; rcp; cmp r, -k, 1.0,
   rcp` (the disassembly in this review's scratch tooling): `cmp` selects
   the constant `r0.y = 1.0` (`def c10 = (0.5, 1, 0, -0.5)`) when `-k ≥ 0`,
   so at k = 0 the colours are multiplied by exactly 1.0 and the `rcp` of
   `1 + 0` is never consumed; the unweigh is `mad(-k·luma + 1); max(·,
   1/65504); rcp; cmp` likewise. Evidence: the tracked
   `verification/results/temporal-pass.txt` differs from `1d36c29` only by
   the added `HDR_CASES` blocks (their `CHECK`/`HDR_*` lines) and the
   `RESULT` count line; every pre-existing line, the 386 samples and the
   camera drifts are byte-identical; `temporal-resolve.txt` is unchanged
   after the rerun (table below). With HDR on and the identity write-back,
   `hdr_taa_k_` is 0 by construction (`motion_output.cpp:1908`:
   `tonemap_active()` false → 0 unless `X3M_TAA_K` overrides); finding 4
   adds the runner check that the identity TAA twins log k = 0 on every
   `hdr_frame` and `motion_output_frame` line, because the fixture's
   reference pass takes the DLL's k from the exposure export
   (`motion_output_fixture.cpp:1230-1239`, `fixture_hdr_exposure` field 7
   = `hdr_taa_k_`, `motion_output.cpp:2218`) and would follow a wrong
   derivation.
2. **Weighting math** - `weigh` is applied to the current pixel
   (`resolve.hlsl:274`), to every finite 3×3 neighbour before the min/max
   box, the mean and the square (`:288`, so mean/σ/min/max live in the
   weighted space), to every Catmull-Rom history tap before the weighted
   sum (`:100`, the single on-grid tap goes through the same function), and
   the blend `lerp(weighted, clamp(old, low, high), w)` is inverted once
   (`:299`). The sentinel policy 1 and every other current-only early
   return hand the unweighted colour through (no blend, nothing to invert);
   policy 2 (camera path at the far plane) goes through the weighted path
   like every other pixel. The inverse of a weighted colour is exact:
   `luma_w = L/(1 + kL)`, `1 − k·luma_w = 1/(1 + kL) > 0`; a convex
   combination of weighted colours keeps `k·luma_w < 1` strictly. Inputs
   are finite and `|v| ≤ 65000` (`finiteColor`) so the weighted domain is
   finite (weights in (0, 1] after finding 1) and every weighted value is
   ≤ 1/k in luma; the FP16 history holds unweighted values ≤ 65000. Two
   holes, both in the negative direction of the assumptions: (a) the
   inputs are not necessarily positive — an FP16 scene keeps the negative
   result of a subtractive blend — and a pixel with `luma ≤ −1/k` made `1 +
   k·luma` zero or negative (finding 1, fixed by flooring the luma at 0 in
   both directions, `resolve.hlsl:62, 86-87`); (b) the per-channel clamp
   (`:298`) can move the history to a box corner whose luma exceeds every
   neighbour's, so "a clipped convex combination keeps k·luma' < 1" is not
   a theorem; the denominator floor at 1/65504 (not 2⁻¹⁶ as the documents
   said) is what keeps the inverse finite there (finding 3, documents and
   the shader header corrected; observation 6 bounds the excess). The
   weighted-space clip is what the design's §3 asks (`hdr-scene-path.md:351-
   353`: weight before the 3×3 mean/σ and the blend, invert after). `k`
   is validated by `prepare` (`resolve.h:67`: finite, `0 ≤ k ≤ 65504`) and
   by the fixture (`hdr_cases`: −1, NaN, 70000 refused).
3. **k derivation** - `hdr_taa_k_` is set at the latch
   (`motion_output.cpp:1908`) after `begin_frame` stepped the exposure and
   `prepare_constants` took the new EV (`hdr_pass.cpp:843-848`):
   `exposure().k()` = `taa_k(exposure())` = `exp2(resolve_ev(mode, manual,
   adapted))`, the same multiplier `prepare(agx_, exposure_.exposure(), …)`
   uploads to the AgX draw of the same frame (`hdr_pass.cpp:818`); manual
   EV → `exp2(manual)`, auto → `exp2(adapted-at-latch)`; the identity
   write-back → 0. `X3M_TAA_K` is parsed once at `initialize_log`
   (`capture.cpp:1269`) into `[0, 65504]` (NaN and Inf fall outside;
   finding 2 adds the end-pointer check so a non-numeric value is ignored
   instead of becoming 0) and reaches the route through `configure_taa_k`
   (`:1152`); `manage.py` rejects `--taa-k` without `--taa --hdr` and
   outside `[0, 65504]` (`manage.py:90-93`) and exports it only when given.
   Logged per frame: `taa_k` on the frame line (`t.k`, `:487`) and `k=` on
   `hdr_frame` (`:1998`), both `hdr_taa_k_`; `taa_k=` on the mode line is
   the override (−1 when derived). The reference pass in case 6 reads
   `fixture_hdr_exposure` field 7 (`:2218`), i.e. `hdr_taa_k_`, at the
   frame's boundary (after the latch, before the resolve), and
   `validate_hdr_taa` (`run_motion_output.py:1395`) checks that value
   against `exp2(ev)` of the same `hdr_frame` line (or the override) and
   that `taa_k` on the frame line agrees — so the DLL's k, the reference's
   k and the write-back's exposure are tied together on the AgX cases;
   finding 4 ties them on the identity cases.
4. **Ordering and state** - Engine hook: `scene_end_hook` runs
   `resolve_hdr(Hook)` then `end_redirect(Hook)` (`:716-717`), before the
   compositor's `GetRenderTarget(0)`; bloom copy: `before_stretch` runs
   `resolve_hdr(StretchRect)` then `end_redirect(BloomCopy)` (`:642`), so
   the application copies the resolved, tonemapped image. `resolve_hdr`
   (`:595-616`) requires `HdrState::Active`, the target and `hdr_main_`,
   records the frame's single attempt through `resolve_allowed` (`:658-
   661`: the second call of a frame returns false, so the 8-bit resolve that
   follows `end_redirect` in both functions is a no-op on a frame the HDR
   resolve handled), checks that RT0 is the FP16 target (else skip 10 as on
   the 8-bit path), takes one container reference for the run and releases
   it (`:611-614`). The pass saves RT0 = the target in its `SavedState` and
   restores it; `end_redirect` → `write_back(..., hdr_resolved_)` then binds
   the chain levels and `main` as RT0 inside its own bracket and restores
   `final_rt0` after (`hdr_pass.cpp:404-425`): the meter chain samples
   `source_texture` (`:404`) and the tonemap draw binds it as texture 0
   (`:414`) — both read the resolved texture; the emergency `StretchRect`
   rung copies `target_` (`:922`), the unresolved scene, documented as such
   (an image, never black). Reference symmetry: `write_back` takes one
   `AddRef` on the caller's texture (`:873`) or the container reference
   and drops it once (`:906`). `hdr_resolved_` is a borrowed pointer to the
   pass's current history texture: set only on a successful run (`:559`),
   cleared at the top of `resolve` (`:486`), of `resolve_hdr` (`:596`), in
   `end_redirect` (`:1965`) and `drop_redirect` (`:1969`); `before_reset`
   drops the redirect before the pass releases its histories
   (`:1106`, `:1111`) and `release_resources` drops it first (`:260`), so
   it never outlives the texture it borrows; between `resolve_hdr` and
   `end_redirect` no pass call can occur (they are consecutive statements
   at both ends). Every end of the redirect writes back when
   `hdr_dirty_ || hdr_resolved_` (`:1961`), so a resolved frame whose scene
   was not dirty (nothing drawn after the last flush) still presents the
   resolved image. TAA off / HDR on: `resolve_hdr` returns before
   `resolve_allowed` (`:597`), `hdr_resolved_` stays null and `write_back`
   takes the container path — stage 2 unchanged (the stage-2 validators
   pass unchanged below). HDR off / TAA on: the 8-bit path with c22 = 0.
   The loosened TAA twin bounds (`run_motion_output.py:937-938`: ≥ 90 %
   exact, ≤ 15 % of material pixels differing, still ≤ 1 code with
   background, flat and alpha exact, `accept_hdr_twin :982-992`) are
   justified by the 8-bit twin's per-frame requantisation of its history
   (the HDR route accumulates unquantised FP16 values; the two histories
   drift by up to half a code before the final rounding, on pixels with
   history only, and more of them the more history frames the script has:
   97.6 % on the seam script with 6 history frames of 12, 92.4 % on the hook
   script with 6 of 7). They cannot mask a defect because the twin
   comparison is not the primary check: on every HDR TAA frame the DLL's
   resolved FP16 image must equal the reference pass's output byte for byte
   (`presented_mismatches` compares the presented 8-bit within one code of
   the reference's own conversion of the same FP16 values,
   `motion_output_fixture.cpp:1247-1261`, and the runner compares the
   `taa_*.rgba16f` readbacks to the `reference_taa_*.rgba16f` files), the
   RT1/RT2 readbacks are identical to the twins', and the k = 0 derivation
   is now asserted directly (finding 4).
5. **Failure paths** - A failed run (`taa_->run` fails, or the fixture's
   injected `HdrFault::Resolve`, `:536`) leaves `hdr_resolved_` null and the
   pass has dropped its history (`fail()` in the pass; `invalidate()` for
   the injected case), `motion_output_taa_failed … hdr=1` is logged
   (`:586-589`, `failure_log_limit`), the frame line carries `taa_hdr=1
   taa_result=…`, and `end_redirect` writes back the unresolved target
   (`hdr_dirty_`): the presented frame is the tonemap of the unresolved
   scene; the next frame resolves current-only. A lost device fails the
   run the same way, and the write-back ladder's lost handling is stage
   1's. `seam-taa-hdr-tonemap-fault` (`TONEMAP_TAA_FAULT_SCRIPT :1459`,
   `validate_hdrtonemapfault_taa :1474`) covers fault 14 at frames 1 and 7
   (unresolved write-back within one code of the AgX reference of the FP16
   readback, history dropped, two failure lines), the tonemap-draw and
   meter faults with the resolve on, a Reset (target and histories
   re-created, current-only, then accumulation) — results below.
6. **Bench sanity** - Rerun once (full suite, table below); the record's
   claims (FP16 resolve cheaper than the 8-bit path at 5120×1440, stage-2
   re-measure about +0.58 ms with the resolve off) are compared with this
   run's medians in the results table.
7. **Docs honesty** - README, `manage.py` help, `live-motion-route.md`,
   `temporal-integration.md`, both HDR documents, `temporal-resolve.md` and
   the `capture.cpp` header say the same thing: with `--hdr --taa` the
   resolve consumes the FP16 scene, the write-back presents the resolved
   image, `k` is the write-back's exposure (0 with the identity
   write-back), proven on the synthetic fixtures and never seen in
   gameplay. Corrected here: the floor value, the "k·luma' < 1" claim, the
   luma floor of finding 1, the word count (4,487) and the fixture counts.
   `docs/status.md` was not edited by this review.

## Findings and fixes

1. **Medium, fixed** - `weigh`/`unweigh` used the raw luma: `finiteColor`
   admits negative values (an FP16 scene keeps the negative result of a
   subtractive blend, which the 8-bit target would have clamped), so a
   pixel with `luma ≤ −1/k` produced a weight of `1/0` (Inf, then NaN in
   the statistics: `square − mean²` of an infinite set) or a negative
   weight (a sign flip, then the inverse's floor and a 65504× gain); the
   3×3 statistics of every pixel around it were poisoned too. Not a
   fixture value before this review (every `hdr_cases` scene was
   non-negative). Reproduced with the new case (5×5 blocks of `(−.5, −.5,
   −.5)` and `(−.5, 1, 0)` among 0.2 greys, stationary, k = 2 and 4) against
   the pre-fix shader: at k = 2 the −0.5 block came back NaN (`finite=0`,
   worst 34,202 ulp; the fixture stops at its first failure so k = 4 was
   not reached — analytically a weight of −1, the block returned at
   32,752). Fixed: `lumaFloored = max(dot(c, lumaWeights), 0)` in both
   directions (`resolve.hlsl:62, 86-87`): a pixel of non-positive luma is
   the identity and its inverse too, every weight lies in (0, 1], the
   round trip stays exact, and k = 0 is untouched (the select is
   unchanged). Program regenerated: 4,487 words (4,375 before), bytecode
   `4cd8ad52…`; the six other programs' bytecodes unchanged (their
   manifests carry the generator's provenance hash only). Fixture case (4)
   added (`temporal_pass_fixture.cpp:1060-1078`), counts 448 / 204 / 386
   (`run_temporal_pass.py` pin updated); the blocks are 5×5 because the
   mean ± 1.25σ clip preserves a stationary value only where at least four
   of its nine taps share it (a block corner has exactly four) — a single
   stationary outlier pixel is clipped toward its neighbourhood at any k,
   0 included (the first form of the case, one pixel, showed −0.213 at
   k = 2 with the fix, the clip's own answer, before it was made a block).
   Documents updated (`temporal-integration.md`, both HDR documents,
   `temporal-resolve.md`).
2. **Low, fixed** - `X3M_TAA_K` was parsed with `wcstof(setting, nullptr)`
   and `0 ≤ v ≤ 65504`; a non-numeric value converts to 0, which is inside
   the range, so `X3M_TAA_K=abc` silently forced the unweighted resolve
   instead of the derivation (the other float switches exclude 0 by their
   ranges; `manage.py` never emits such a value). Fixed: the conversion
   must consume the whole string (`capture.cpp:1269`).
3. **Low, fixed (docs)** - The shader header, `temporal-integration.md` and
   the stage-3 table said the inverse denominator is "floored at 2⁻¹⁶" (it
   is `1/65504`) and that "a clipped convex combination keeps k·luma' < 1"
   (true for the convex combination, not for the per-channel clamp that
   follows it, checklist 2b); corrected to say what holds: the inverse of
   a convex combination is exact, the floor is the guard for the box-corner
   case, the output is then finite but large, never Inf or NaN.
4. **Low, fixed (runner)** - On the identity-write-back TAA twins
   (`production-taa-hdr-on`, `seam-taa-hdr-on`, `seam-taa-envmap-hdr`) the
   reference pass is fed the DLL's own k and the twin bounds admit one code
   of drift, so a wrong derivation (k ≠ 0 without an exposure model) would
   have been followed by the reference and could pass the twin comparison
   at small k. Added `validate_identity_k` (`run_motion_output.py:1435`,
   hooked at `:1931`): every `hdr_frame` line reports k = 0, every frame
   line with `taa_hdr=1` reports `taa_k` = 0 (or the `X3M_TAA_K` value),
   and the `hdr_tonemap` line says `tonemap=0`.
5. **Low, fixed (runner; external cause)** - `a8d4309` gates the per-draw
   telemetry metrics (`RouteGate`, `RouteDraw`, `RouteSetRenderTarget`,
   `RouteJitter`, `RouteLazyFlush`, `DrawBackend`) behind a new
   `X3M_TELEMETRY_DRAW=1` (`telemetry.cpp:16-36`), while
   `run_motion_output.py` exported only `X3M_TELEMETRY=1` and asserts
   `route_draw_us > 0` (`:721`); the first full run after that commit
   failed on `production-on` frame 0 with every per-draw cost at 0 and
   `telemetry_start … per_draw=0`. Not a stage-3 effect (the same case
   passed at 17:11 before the commit, `telemetry_start` then carrying no
   `per_draw` field). Fixed by exporting `X3M_TELEMETRY_DRAW='1'` in the
   runner's child environment (`:1859`), which restores the runner's prior
   semantics; the owner of the telemetry change should confirm that is the
   intended default for the fixtures.
6. **Observation** - After finding 1 the only path to a denominator at
   the floor is the box corner of checklist 2b: the clamped history's
   channels are each bounded by `max(old_c, low_c)` and by `high_c`, so
   its luma is below `L(N₁) + w_b·N₂_b` for two neighbours `N₁`, `N₂` of
   different chromaticity, i.e. below `2/k` in the extreme (adjacent
   saturated primaries several stops over the exposure, with a history of
   a third chromaticity), and the blend with the current pixel (weight
   0.9) needs `k·L(old') > 1.11` to reach the floor at all. The effect is
   one pixel, one frame, in a region the tonemap already clips; the next
   frame's weighting bounds the stored value again and an FP16 overflow
   (possible for k < 2) is dropped as a non-finite tap. A luma clamp of the
   clipped history to the brightest weighted neighbour would remove the
   case at about 25 instructions; not changed, recorded in the shader
   header and `temporal-integration.md`.
7. **Observation** - `exposure.cpp:79` (`luma_weight`) and
   `exposure_reference.py:193` keep the unfloored formula; nothing on the
   shader path consumes them (the CPU model in `temporal_pass_fixture.cpp`
   is grey-only and positive), and they are stage-2 files outside this
   review's edit scope. Align them with the floor when the reference is
   next touched.
8. **Observation** - `X3M_TAA_DEBUG` on the HDR path flushes the
   unresolved scene into the main target before the resolve
   (`motion_output.cpp:491`) so the 8-bit "pre-resolve colour" readback is
   meaningful; that flush is a full write-back (tonemap and chain) paid
   only in capture frames with the debug switch. Documented in the stage-3
   table; not changed.
9. **Observation** - `resolve()` sets `t.copy = S_FALSE` on the HDR path
   (no copy-back) and the runners assert `taa_copy=00000001` there; a
   reader of the frame line should know `S_FALSE` means "not applicable",
   which the stage-3 text says. Not changed.

## Results after the fixes

| Suite | Result |
| --- | --- |
| `cmake --build build --clean-first -j4` (by `run_motion_output.py`, twice: the first full attempt and the clean rerun) | OK both times (RelWithDebInfo, `-Wall -Wextra`, 0 warnings; the seam DLL and fixture with `-Werror`, 0 warnings). The temporal, ownership and scene-capture suites ran between the two builds on the first DLL (`44563f32…`); none of them links or loads `build/d3d9.dll` (they build their own fixtures), and its hash was unchanged after each. The rerun's DLL differs (`4fe061f1…`) only because the other agent's `mesh_adjacency_fast.*` sources, compiled into it, changed in between; the x87 check below is on the final one |
| `run_motion_output.py` (full, clean rebuild, after findings 1–5; the recorded pass is the second full run) | PASS: 90 runs (78 cases + 12 bench), all exit 0, every validator including the new `validate_identity_k` (`production-taa-hdr-on`, `seam-taa-hdr-on`: 9 `hdr_frame` lines and 9 HDR resolves each, k = 0 throughout), sources and binaries unchanged during the run. The first full attempt, 35 minutes earlier, had the same 90 runs at exit 0 with case numbers identical to the ones below and failed only its final provenance assertion (the mesh-adjacency agent edited the untracked `src/proxy/mesh_adjacency_fast.{cpp,h}` during it; its binaries' hashes were unchanged at the end); two earlier attempts were refused by the runner's own game guard when the user launched the game mid-suite, and one stopped on finding 5 before the fix. Stage-1 twins: the four plain twins 99.01 % exact (489 of 27,416 material pixels, max 1 code, background/flat/alpha exact) as in review 23; the TAA twins 98.93 % (`production-taa-hdr-on`, 526 / 27,346), 97.63 % (`seam-taa-hdr-on`, 1,163 / 27,421), 92.42 % (hook, 2,172 / 18,714), 98.97 % (envmap, 210 / 13,527), all max 1 code with background/flat/alpha exact — the stage-3 record's numbers. Stage 2 unchanged: `seam-hdr-values` 26 checks, `seam-hdr-fault` 119, `seam-hdr-tonemap-fault` 59, `seam-hdr-tonemap-shader-absent` 23, caps/self-test-absent identical to `seam-on`. Stage 3: `seam-taa-hdr-tonemap-on` / `-ev1` / `-auto` / `-k0` / `seam-ownership-taa-hdr-tonemap-on` k per frame 1.0 / 2.0 / 1.084 → 1.626 / 0.0 / 1.0, presented vs AgX(resolved) max 0.500 code, mean 0.269 / 0.240 / 0.286 / 0.269 / 0.269, fixture worst 0 codes (the resolved FP16 image equals the reference pass byte for byte), history frames 1, 2, 4, 7; `seam-taa-hook-hdr-tonemap-on` 127 checks, `production-taa-hdr-tonemap-on` 59, ownership variant 140 at zero final references; `seam-taa-hdr-tonemap-fault` 132 checks, resolves failed at frames 1 and 7 (`80004005`, `hdr=1`), presented frames within 0.5001 code of the AgX reference of the unresolved (1, 7) or resolved (2–6, 8) FP16 readback, one `tonemap` unwind, recheck 4 passed, one Reset with two target creations. Bench medians / minima (ms, the recorded run): 1280×768 — 8-bit TAA off 0.425 / 0.335, on 0.783 / 0.631; HDR identity off 0.406 / 0.367, on 0.794 / 0.642; AgX + meter off 1.031 / 0.851, on 1.238 / 1.189; 5120×1440 — 8-bit off 0.564 / 0.435, on 2.317 / 2.205; HDR identity off 0.919 / 0.853, on 1.935 / 1.873; AgX + meter off 1.453 / 1.423, on 2.260 / 2.169 (the first attempt, with other agents' native builds sharing the CPU: 1.830 / 3.097 in the two AgX cells at 5120×1440, the rest within 0.1 ms; observation 10) |
| `python3 -m unittest discover -s verification/analysis` | 673 tests, 672 OK, 1 import error outside stage 3: `test_admission_integration_expectations` loads `verify_ownership_integration.py`, whose new `import bottle` (another agent's in-flight module) is not on the test's path; rerun after the other suites: 675 tests, 674 OK, the same single import error (`test_exposure_port.py` and `test_agx_reference.py`, the two that pin stage-2/3 artifacts, pass; the resolve manifest pin is the generator `--check` row) |
| `check_no_x87.py build/d3d9.dll` | PASS on the final DLL (`4fe061f1…`) and on the first (`44563f32…`): 130 reachable functions from the seven light roots (129 in review 23; the added one is outside stage 3), 0 violations |
| `run_temporal_pass.py` | PASS: 448 numerical / 204 state checks (416 / 164 of review 23 + the stage-3 `hdr_cases` + finding 1's case), 386 samples, 2 generations; camera drift (px) static 0.069, yaw 0.124, pitch 0.176, yaw unjittered 0.062, narrow 0.026, single step 0.033 / 0.029, identity control 0.862, swapped control 1.051, after the cut 0.100 — identical to review 23; negative controls (oracle errors 0.423 / 0.454 / 0.287) intact; sources, binary and compiler unchanged during the run; `temporal-pass.txt` differs from `1d36c29` only by the added `HDR_CASES` blocks (138 `CHECK` and 26 `HDR_*` lines) and the `RESULT` count line — the k = 0 identity evidence. Stage-3 cases: firefly 8.0 over 0.2 at k = 0 / 1 / 4 as in the record; gradient worst ulp 0 / 1 / 1; edge 1 / 39.98; finding 1's negative blocks at k = 2 and 4: worst 1 ulp, every output finite, centre −0.5 exact, mixed pixel −0.499756 (one ulp) |
| `temporal_run.py` | PASS: 78 / 78 sample checks, reset passed, 2 device generations, executable unchanged; `temporal-resolve.txt` byte-identical to `1d36c29` (the fixture uploads c22 = 0 explicitly) |
| `run_ownership_integration.py` | PASS on the rerun: 26 runs exit 0, build and verification manifests written, source tree unchanged during the run, `build/d3d9.dll` not relinked (hash unchanged). The first attempt, in the same chain as the other suites, had its 26 runs at exit 0 too and failed only the same provenance assertion as the motion-output run (the mesh-adjacency agent editing `src/proxy/mesh_adjacency_fast.*` during it) |
| `run_scene_capture.py` | PASS: 4,908 checks, 16 samples, 36 scenarios (unchanged) |
| `generate_rigid_motion_pixel.py --check` | PASS: seven programs recompiled and equal to the checked-in artifacts (`temporal_resolve` 4,487 words `4cd8ad52…` after finding 1; `hdr_tonemap` 414 `f5e78954…`, `hdr_meter_level0` 1929 `4a59a0d1…`, `hdr_meter_reduce` 392 `3de611fb…`, `hdr_writeback` 46 `9cbb62f6…`, `rigid_motion` 176 `a604ce8c…`, `current_depth` 35 `d097c156…`) |
| `manage.py --dry-run` | `--taa-k 1` without `--taa --hdr` rejected; `--taa-k 70000` rejected; `--taa-k 2.5` and `--taa-k 0` export `X3M_TAA_K=2.5` / `0.0`; without `--taa-k` the variable is not exported (the DLL derives k) |

Bench reading (checklist 6): both claims of the stage-3 record reproduce
on the recorded run. At 5120×1440 the HDR identity boundary with the
resolve on (1.935 ms) is cheaper than the 8-bit TAA boundary (2.317 ms),
−0.38 ms against the record's −0.3 ms, and the resolve increments agree
within 0.1 ms (8-bit +0.36 / +1.75, identity +0.39 / +1.02, AgX +0.21 /
+0.81 at 1280×768 / 5120×1440). The stage-2 re-measure (AgX + meter over
identity, resolve off, 5120×1440) is +0.53 ms median / +0.57 min against
the record's +0.58 / +0.57, and +0.33 / +0.30 with the resolve on against
+0.38 / +0.32; at 1280×768 +0.63 / +0.48 off and +0.44 / +0.55 on (the
record: +0.45 / +0.55). The first attempt, run while other agents' native
builds shared the CPU, gave +0.95 and +1.16 ms for the two 5120×1440 AgX
cells with every other cell within 0.1 ms of the rerun; see observation
10.

10. **Observation (cost)** - the AgX + meter increment at 5120×1440 with
    the resolve off now stands at +0.50 (stage-2 record), +1.72 (review 23),
    +0.58 / +0.59 / +0.68 (the stage-3 record's three runs), +0.95 (this
    review's first attempt, other agents' native builds sharing the CPU) and
    +0.53 ms (the recorded rerun, quieter machine); the 8-bit and identity
    cells reproduce within 0.1 ms in every one of those runs. The 20-sample
    bench of a chain of seven small draws is sensitive to what else the
    machine is doing, which the identity and 8-bit cells, one large draw or
    copy each, are not: the chain's cost is the submission of the draws,
    i.e. CPU time that contention inflates. Quote +0.5–0.6 ms for the chain
    at 5120×1440 from a quiet run and treat the larger figures as load; the
    cheaper-chain proposal in the stage-3 record stands.

Final `build/d3d9.dll` SHA-256: `4fe061f1f537d423cd9a782a80f84f62b40149253b377820f98e1c21b2c10160`
(seam DLL `836d52fc442ace07ced275a47c3c066bcc055f6edfc349aa5a704e31dfc99c59`, fixture `1ebdb4f4a18d0020365970ba58ea28ce6b6d22fbce879e823ce1d7c31267f81d`; the first build's `44563f32…` / `25bfc848…` / `df8da9c1…` carried the temporal, ownership and scene-capture runs).

Verdict: go for stage 3 as committed in `a8d4309` with findings 1–5
applied (the luma floor with its fixture case and regenerated program, the
`X3M_TAA_K` parse, the corrected documents, the identity-k runner check,
the per-draw telemetry switch in the runner), `--taa-k` left unset by
default (k derived from the write-back's exposure; 0 with the identity
write-back, which keeps `--hdr --taa` without `--hdr-tonemap` within one
code of the 8-bit route). What the evidence supports: the 8-bit route is
untouched bit for bit (the tracked temporal reports, the k = 0 select in
the bytecode, the 8-bit motion-output cases at their review-23 counts); on
the FP16 path the DLL's resolved image equals the reference pass byte for
byte at every k the fixtures exercise (0, 1, 2, an adapting 1.08–1.63,
the override), the presented frame is the AgX transform of that resolved
image to the last bit an 8-bit code holds, the meter reads the resolved
image, k is the write-back's own exposure multiplier on every frame, the
weighting suppresses a firefly by the analytic factor and preserves
stationary HDR content — negative channels now included — within one
FP16 ulp, a failed resolve presents the unresolved scene and drops the
history, and a Reset re-creates target and histories. What it does not
support: any gameplay claim (nothing here was seen in the game; the
weighting's effect on real engine content, its interaction with the
game's own negative-blend pixels and the box-corner case of observation 6
are untested beyond the synthetic scenes), the chain's cost at 5120×1440
beyond the +0.5–0.6 ms of a quiet run (observation 10), and the runner's telemetry
switch as the telemetry owner's intended default (finding 5). The first
game evidence is the user-run flight with `--motion-output --taa
--telemetry --hdr --hdr-tonemap`: read `taa_hdr=1 taa_k=` on the frame
lines against `hdr_frame … ev=`, any `motion_output_taa_failed … hdr=1`,
and whether bright edges (engine glows, laser fire over a dark hull) show
less flicker than the 8-bit route without smearing, before `--taa-k` or
the weighting default is revisited.
