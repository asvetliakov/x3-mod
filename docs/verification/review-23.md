# Review 23: FP16 HDR scene path, stage 2

Independent review of the uncommitted tree on top of `0b87007`, limited to
stage 2 of the HDR scene path: the AgX write-back and the exposure meter in
`src/renderer/hdr_pass.{h,cpp}` (tonemap ladder, chain, self test, the
c0..c21 bracket), the host exposure port `src/renderer/exposure.{h,cpp}`
against `tools/analysis/exposure_reference.py`, the three `ps_3_0`
programs (`src/temporal/agx.hlsl`, `hdr_meter_level0_ps.hlsl`,
`hdr_meter_reduce_ps.hlsl`) with their generated headers and manifests, the
switch parsing and telemetry in `motion_output.{h,cpp}`, `capture.cpp`,
`telemetry.{h,cpp}`, `manage.py`, the fixture scripts `hdrramp`,
`hdrexposure` and `hdrtonemapfault`, their runner validators, the two
analysis tests and the documents. No game was launched; one Wine runner at a
time (`game_guard` and `pgrep -fl 'verification/probe'` before every
launch); result files were queried with scripts, never read whole.

## Checklist

1. **Identity default** - `X3M_HDR=1` alone is stage 1 bit-for-bit: the
   `copy_draw` diff adds work only behind `program != nullptr`
   (`hdr_pass.cpp:384` saves c0..c21 only then, `:404` runs the chain only
   with `program->meter`, `:410-413` select the identity program and skip
   the constant upload without one), `write_back` takes the unchanged
   stage-1 call when `tonemap_active()` is false (`:895`), `begin_redirect`
   calls `begin_frame` only with the tonemap active
   (`motion_output.cpp:1838-1846`), `attach` creates the tonemap and meter
   programs only with `config_.tonemap == Agx` (`hdr_pass.cpp:748`) and the
   chain only with `caps_.meter` (`:292`); `prepare_constants` at attach
   writes a struct member and nothing else. The eight stage-1 twins, the
   value and fault scripts, the caps/self-test-absent runs and the identity
   bench were rerun unchanged in the suite below (same exact-pixel counts as
   review 22). With `X3M_HDR` unset: `hdr_` is not constructed
   (`motion_output.cpp:821`), the switch parsing runs once at
   `initialize_log` (`capture.cpp:1262-1283`) into a 60-byte `HdrConfig`,
   `log_hdr_frame` is behind `hdr_enabled_` (`:2135`), and no new hook, no
   allocation and no per-draw compare were added (the per-draw path is the
   stage-1 enum compare).
2. **Shader correctness** - `agx.hlsl` follows `agx_reference.tonemap_engine`
   stage by stage (decode → `min(clamp)` → `× exp2(EV)` → inset → log
   encode with the `1e-10` floor and the `[MIN_EV, MAX_EV]` clamp →
   sixth-order contrast → CDL look with `max(·, 0)` under the power →
   outset → `saturate`, alpha carried unsaturated as the 8-bit target
   clamps it; the constants come from `agx.h`, pinned by
   `test_agx_reference.py`). The `1e-10` floor under the decode `pow` is
   harmless for the gamma and sRGB branches (`1e-22` against the reference's
   exact 0, both below the log floor) but the floored value was also fed
   to the `none` branch, so a negative input in the A/B mode decoded to
   `1e-10` instead of passing through (finding 1, fixed in both programs).
   Register layout: the tonemap reads c8..c21, the meter c0..c3, the
   bracket saves and restores c0..c21 as one block (`hdr_pass.cpp:337,
   366`) only when a stage-2 program runs; the identity program reads no
   constant; c4..c7 are untouched; the resolve's c0..c7 are outside the
   bracket. Meter chain: level 0 takes the 16 taps of the 4×4 block at
   source texel centres (`(base + t + 0.5) / size`, `base = floor(uv ×
   outputSize) × 4`, the -0.5 quad landing `uv × outputSize` on `i + 0.5`)
   with CLAMP addressing, so the edge weighting equals
   `exposure_reference.reduce_chain`'s `min(·, size - 1)`; the 4×4 self test
   is one level-0 draw into the 1×1 whose sixteen taps are equal, exact by
   construction (6.0 = log2 of the clip); the general case is a float32 mean
   of values in `[log2(1e-4), log2(64)] = [-13.29, 6]` at every level, so
   the 1×1 stays in that range. NaN/Inf: the only non-finite source is a
   non-finite scene pixel (decode of +Inf gives +Inf, clipped to 64 by the
   `clamp`; a negative or -Inf gives the floor; `pow` never sees a zero or a
   negative base), and a NaN's fate under the backend's `min`/`max` is
   unspecified, which is why the host rejects a non-finite readback before
   the step (`:842`); a rejected meter holds the state (no poisoning, no
   step), a swallowed NaN yields an in-range mean. The fixture had no such
   pixel: finding 2 adds ten frames of hazard and NaN blocks and the runner
   checks both outcomes.
3. **Exposure port** - `exposure.cpp` against `exposure_reference.py`
   function by function: `decode_channel` (`max(·, 0)` then the gamma or
   the piecewise sRGB), `luma`, `meter_level0`/`meter_clipped` (floor and
   clip from the params), `reduce_mean` (double accumulation), `reduce_chain`
   (edge-clamped taps, factor 4), `ev_target` (`log2(key) - avg + offset`,
   clamped; a non-positive key or a non-finite average returns the clamped
   0 where the reference raises), `clamp_dt` (non-finite → `dt_max`),
   `adapt_rate` (`1 - exp2(-dt / (tau ln 2))`; a non-positive tau returns 1
   where the reference raises), `adapt` (dt clamped inside, `tau_down` when
   the target is below the state, the result clamped), `exposure_multiplier`,
   `resolve_ev` (manual → the manual EV, 0 if non-finite), `taa_k`,
   `luma_weight`, `weight_color`, `unweight_color`; `ExposureState::step`
   is `simulate`'s loop body. One deliberate extra: `configure` clamps the
   manual EV to `[ev_min, ev_max]` (`exposure.cpp:92`) so `exp2(EV)` stays
   inside `prepare`'s range; the `hdr_tonemap` line prints the requested
   value and `hdr_frame … ev=` the effective one (documented now, finding 3).
   `test_exposure_port.py` compiles the port natively and replays 23 cases
   through the reference (the frame loops to 1e-5 EV). Readback: the chain
   ends in a 1×1 ring target and returns (`hdr_pass.cpp:482` marks the slot
   pending); `GetRenderTargetData` and the `LockRect` are issued at the next
   latch only (`:836-839`), one Present later; nothing waits on the current
   frame (the runner's `readback_us` 30–80 µs against the 0.7 ms measured
   for an immediate copy). Ring and readback surfaces: `D3DFMT_R32F`
   render-target textures (level 0 kept) and `D3DPOOL_SYSTEMMEM` offscreen
   plain surfaces (`:264-266`), lockable by definition. The lock is unlocked
   on the one path that locked it (`:839`); a failed copy or lock skips
   both the unlock and the step, clears `pending` and is reported
   (`readback=` on the frame line); a lost device fails the copy the same
   way. Reset: `before_reset` → `release_target` → `release_chain` drops the
   levels, the ring and the readback surfaces and clears `pending`
   (`:211-215`); the next latch's `ensure_target` recreates the chain
   (`:292`); the exposure state survives a Reset at the same size and is
   reset by a dimension change (`:290`, documented in "Stage 2
   implementation"); the failure counter and the chain demotion persist for
   the device (documented).
4. **State bracket** - one `save`/`restore` around the chain and the tonemap
   (`copy_draw`): texture 0 unbound, RT1.. unbound before RT0 changes
   (`:392`, the review-22 rule; the chain's small levels are bound as RT0
   with RT1.. and the depth surface already null), fixed FVF, samplers,
   stage states, render states, the chain (`:438-484`: per level RT0,
   viewport, program, c0..c3, texture = the previous level, whose container
   is obtained per use and dropped before the next), texture 0 unbound
   again, RT0 = destination, viewport, program, c8..c21, texture = scene,
   the quad, then `restore` with c0..c21 (`:366`), RT0 = `final_rt0` and
   viewport/scissor after the targets. Nothing is left bound: the fixture's
   state comparisons around every frame (`restorations` counted in the
   tonemap-fault case) and the twins' colour hashes after the boundary
   prove it on the fixture; the meter's `GetRenderTargetData` at the latch
   binds nothing.
5. **Ladder** - (1a) the AgX draw; on `FAILED(draw) && !lost && restore ok`
   the identity draw (`:885-889`, `tonemap_failures_++`, reported as unwind
   `tonemap` with `fallback=1` and no rebind because the image is complete,
   `:903`); a failed restoration or a lost device skips the fallback and
   takes the stage-1 rungs `StretchRect` (`:912-914`) → `bind` (`:922`).
   After three failed AgX draws `tonemap_active()` is false
   (`hdr_pass.h:125, 183`) and every later write-back is the identity
   draw; the meter stops with it (`meter_active()`); one
   `hdr_tonemap_disabled` line. Recheck: the `tonemap` unwind blocks the next
   latch until the self test passes (the stage-1 mechanism), and the self
   test's stage-2 sections demote the tonemap or the meter without refusing
   the feature. No rung leaves the main target unwritten when a copy rung
   works; no black-frame path exists (a failed meter never fails the image,
   `:404` reports it in the program only).
6. **Cost** - the +0.35–0.5 ms per frame at 1280×768 / 5120×1440 is the
   chain: six (seven) small draws at ~25 µs of submission each plus the
   deferred 4-byte copy and lock (36–74 µs); the AgX draw itself is inside
   the identity draw's spread (manual-EV bench). The cheaper chains are a
   follow-up, not implemented here: 8× per axis (64 taps per output texel,
   three draws at 1280×768, `[unroll]` of 64 `tex2Dlod` fits `ps_3_0`) would
   halve the submissions; metering a 1/4-resolution copy does not remove a
   draw. Neither is trivial to prove against the reference in one review
   cycle (the reference chain is factor-parametrised, the fixture scenes are
   64×64 so a factor-8 chain needs new sizes), so both stay in the design
   notes for a gameplay profile to justify.
7. **manage.py, telemetry, x87** - `--hdr-tonemap` without `--hdr` and any
   sub-switch without `--hdr-tonemap` are rejected; `--hdr-ev-manual 20`
   and `--hdr-clamp 70000` are rejected; the accepted form exports the six
   `X3M_HDR_*` variables (`X3M_HDR_EV_MANUAL=''` when unset, which
   `GetEnvironmentVariableW` reports as absent). `capture.cpp:1262-1283`
   parses with range checks (`wcstof` garbage, NaN and Inf fall outside
   every range and are ignored; `ev_min > ev_max` collapses to `ev_max`).
   Telemetry: three bools, three HRESULTs and two tick counters added to the
   per-frame `MotionHdrCounters`, two metric names with the `static_assert`
   on `Metric::Count`, every string field a literal. x87: the host exposure
   math compiles to SSE2 except the i386 float-return transports
   (`flds`/`fstps` in every `float`-returning function of `exposure.cpp`)
   and the `uint64 → double` conversion of the QPC interval in `begin_frame`
   (`fildll`/`fstpl`/`fadds`, GCC's unsigned fix-up); both execute only
   under the full boundary: every path into `begin_redirect` and
   `write_back` is a `CpuCallBoundary` hook (`capture.cpp:629` Clear, `:663`
   SetRenderTarget, `:689` StretchRect, `:721` EndScene, `:463` Present,
   `:516` Reset, `:782/:801` UpdateSurface/ColorFill, `:1046` GetRenderTarget
   and the GetRenderTargetData hook) or the engine scene hook under
   `PreserveCpuState` (`scene_hook.cpp:37`, FNSAVE/FRSTOR). `check_no_x87`
   walks the seven light roots only and reaches none of it by construction
   (PASS below). The QPC conversion could avoid the `fild` by converting the
   32-bit delta (observation 6); not changed, the boundary covers it.
8. **Docs honesty** - README, `manage.py` help, `live-motion-route.md`, both
   HDR documents and the renderer/temporal READMEs say the same thing: with
   `--hdr-tonemap` the presented image is the AgX transform of a gamma-space
   FP16 scene decoded per §2 (a documented approximation), still LDR to the
   game's bloom and GUI, proven against the Python reference on synthetic
   ramps and blocks and never seen in gameplay; without it stage 1 is
   unchanged. `docs/status.md` was not edited by this review.

## Findings and fixes

1. **Low, fixed** - `decodeEngine` in `agx.hlsl` and
   `hdr_meter_level0_ps.hlsl` applied the `1e-10` floor before the branch
   select, so `X3M_HDR_DECODE=none` returned `max(e, 1e-10)` while
   `agx_reference.decode('none')` returns the raw value; a negative channel
   (possible from a subtractive blend, never from the fixture's ramps, which
   are non-negative) then took a different path through the inset and the
   meter's luma. Fixed: the floor feeds the gamma and sRGB branches only and
   the `none` branch reads the raw input (`agx.hlsl:67-73`,
   `hdr_meter_level0_ps.hlsl:37-43`); both programs recompiled and pinned
   (`hdr_tonemap` 414 words, bytecode `f5e78954…`; `hdr_meter_level0` 1929
   words, `4a59a0d1…`; `hdr_meter_reduce` unchanged), manifests and the
   architecture table updated.
2. **Low, fixed; one behaviour recorded** (checklist 2) - no fixture pixel
   was negative, infinite or NaN, so the host-side `isfinite` rejection of
   the 1×1 readback and the shaders' floors and clips on such inputs were
   untested. Fixed: `hdrexposure` grew from 30 to 40 frames
   (`motion_output_fixture.cpp`, `run_hdrexposure`): frames 30–34 draw
   `-1 / +Inf / -Inf / 0.18` blocks, frames 35–39 a NaN block with three
   mid-grey ones; the capture window is moved onto frames 30–37 (the DLL
   caps it at eight) so the stored FP16 values are on record. The runner (`validate_hdrexposure`) checks
   the finite negative block against the reference (decoded to the floor:
   black, metered at `log2(1e-4)`), requires every printed state value
   finite, accepts either outcome for a frame whose meter contained an
   unspecified value (a held state, `stepped=0`, or a step on an in-range
   mean) with the `steps` counter consistent, compares the finite blocks of
   those frames with the reference, and records the rest (`hazard` in the
   summary). What the first run found: on this backend `+Inf`, `-Inf` and
   NaN behave alike, presented white (`ffffffff`) and metered at the floor
   (the meter of the hazard frame is exactly `(3 × log2(1e-4) +
   log2(luma(0.18^2.2))) / 4 = -11.327`), where the reference says `+Inf` →
   clip and white, `-Inf` → floor and black. Since `max(-Inf, 1e-10)` itself
   did not floor, a shader-side clamp would rest on the same unspecified
   `min`/`max` and was not attempted; the production guard is the host
   check, which held (the NaN frames stepped on the swallowed floor value,
   state finite throughout). No gamma-space game content reaches 65504,
   so no infinity is expected from the scene; documented in both HDR
   documents. Results below.
3. **Low, fixed (docs)** - `X3M_HDR_EV_MANUAL` is clamped to
   `[ev_min, ev_max]` (`exposure.cpp:92`) while `manage.py` accepts ±16 and
   the `hdr_tonemap` line prints the requested value; the switch table and
   the `--hdr-ev-manual` help now say so. The verification record called
   the 64×64 chain "three level surfaces": it is two level surfaces (16×16,
   4×4) plus the two 1×1 ring targets (`chain_bytes` 1,096 confirms);
   corrected, and the `hdrexposure` frame count updated in both documents.
4. **Observation** - a recheck (mid-frame self test while blocked) that
   demotes the tonemap or the meter leaves `tonemap_shader_` or the meter
   programs and the chain allocated until `shutdown` (`attach` drops them
   only at attach time); they stay counted in `references()`, so the
   accounting is consistent and only the memory (two shader objects, the
   chain levels) lingers. Not changed.
5. **Observation** - a mid-frame flush runs the chain on the partial scene
   and the end write-back runs it again into the same ring slot, so the
   consumed meter is the last write of the frame (the scene end); the cost
   of the extra chain is paid per flush (the fixture's TAA/hook frames have
   one flush). Documented in "Stage 2 implementation"; a follow-up could
   skip the chain on flushes.
6. **Observation** - `begin_frame` converts `now_ticks - latch_ticks_`
   (`uint64`) to `double`, which GCC compiles to `fild`+`fadds` on i386
   even with `-mfpmath=sse`. Under the full boundary this is safe (checklist
   7); converting the 32-bit delta (an interval above 2^31 ticks is clamped
   to `dt_max` anyway) would make the function x87-free. Not changed.
7. **Observation** - the meter chain samples the scene with
   `D3DTADDRESS_CLAMP`, so for a scene width that is not a multiple of 4
   the last column of the first level weights the edge texels twice or
   more, as the reference's `reduce_chain` does; 1280×768 and 5120×1440
   are exact, 1366×768 is not, and the deviation from the exact mean is the
   reference's own. `test_exposure_port.py` covers the 13×7 case.
8. **Observation** - the manual-EV clamp (finding 3) and the
   `prepare` fallback to exposure 1 (`prepare_constants`) can never both
   fire: `exp2(±8) ≤ 65504`.
9. **Observation, open (cost)** - in this run the 5120×1440 bench with the
   resolve off measured 2.653 ms median with the AgX write-back and the
   meter against 0.936 ms for the identity twin (+1.72 ms; every one of the
   20 timed samples between 2.34 and 3.46 ms), where the stage-2 record
   measured +0.50 ms for the same configuration on the same day; the other
   three configurations reproduce the record (+0.33 / +0.33 / +0.36 ms).
   The CPU phases of the timed frames are small (`meter_us` 282,
   `readback_us` 134, `writeback_draw_us` 322), so the excess is GPU-side
   inside the EVENT-synchronized boundary, and finding 1 changed two
   register moves, not the chain's work. Not re-measured here (a bench-only
   rerun rebuilds the DLL, which would have invalidated the hash the other
   suites ran on); the number is recorded as measured and the stage-2
   record's cost table is annotated. Repeat the two 5120×1440 benches
   before treating either figure as the chain's cost at that size.

## Results after the fixes

| Suite | Result |
| --- | --- |
| `cmake --build build --clean-first -j4` (by `run_motion_output.py`) | OK (RelWithDebInfo, `-Wall -Wextra`, 0 warnings); the seam DLL and fixture with `-Werror`, 0 warnings; no later runner relinked `build/d3d9.dll` (hash checked after every suite) |
| `python3 -m unittest discover -s verification/analysis` | 568 tests OK (`test_exposure_port.py` compiles the port natively; `test_agx_reference.py` pins the three recompiled manifests) |
| `check_no_x87.py build/d3d9.dll` | PASS: 129 reachable functions from the seven light roots, 0 violations |
| `run_motion_output.py` (full, clean rebuild; the recorded pass is the second full run, after findings 1–2 were applied) | PASS: 82 runs (70 cases + 12 bench), all exit 0. Stage-1 twins unchanged: 48,663 / 49,152 exact (99.01%) in the four plain twins, 48,626 / 48,602 (TAA), 28,273 / 28,672 (hook), 20,270 / 20,480 (envmap), max 1 code; `seam-hdr-values` 26 checks; `seam-hdr-fault` 119 checks, unwinds `draw/stretch, restore/shader, stretch/restore, lost/restore, draw/stretch`; caps-absent and self-test-absent identical to `seam-on`. Stage 2: ten `hdrramp` runs (780 cells each) max code error 0.485–0.500, mean 0.082–0.242 per channel, alpha exact, 11 checks each (the `decode-none` ramp with the recompiled program 0.498 / 0.499 / 0.499 max); `hdrexposure` ×3 (85 checks, 40 frames): meter max error 0.00215 log2 (0.063%), EV replay 1.2e-6 / 9.4e-7 EV, end to end 7.3e-4 / 1.7e-3, presented max 0.53 / 0.55 code, sun clip 2.15 stops, hazard frame 30 stored `-1 / +Inf / -Inf / 0.17993` (FP16 truncation) presented `ff000000 / ffffffff / ffffffff / ff393939`, consumed meter -11.3268; NaN frame 35 stored `nan` presented `ffffffff`, consumed meter -7.4048 (the floor swallowed), every state value finite, all 9 unspecified frames stepped in range, ownership variant identical and at zero references; `seam-hdr-tonemap-fault` 59 checks, unwinds `tonemap ×3`, rechecks 2 / 6 / 7 passed, images max 0.5 code; `seam-hdr-tonemap-shader-absent` 23 checks, `tonemap_reason=shader meter_reason=tonemap`. Bench medians (identity → AgX + meter): 1280×768 resolve off 0.434 → 0.766 ms, on 0.851 → 1.180 ms; 5120×1440 off 0.936 → 2.653 ms, on 2.562 → 2.920 ms (observation 9) |
| `run_temporal_pass.py` | PASS: 386 samples, 164 state restorations, 2 generations; camera drift (px) static 0.069, yaw 0.124, pitch 0.176, yaw unjittered 0.062, narrow 0.026, single step 0.033 / 0.029, identity control 0.862, swapped control 1.051, after the cut 0.100 |
| `temporal_run.py` | PASS: 78/78 sample checks, reset passed, 2 device generations |
| `run_ownership_integration.py` | PASS: 26 runs exit 0, build report `PASS`, verification report `PASS`, sources and binaries unchanged during the run |
| `run_scene_capture.py` | PASS: 4,908 checks, 16 samples, 36 scenarios |
| `generate_rigid_motion_pixel.py --check` | PASS: seven programs recompiled and equal to the checked-in artifacts (`hdr_tonemap` 414 words `f5e78954…`, `hdr_meter_level0` 1929 words `4a59a0d1…`, `hdr_meter_reduce` 392 words `3de611fb…`, `hdr_writeback` 46 words `9cbb62f6…`) |
| `manage.py --dry-run` | `--hdr-tonemap` without `--hdr` rejected; `--hdr-look golden` without `--hdr-tonemap` rejected; `--hdr-ev-manual 20` and `--hdr-clamp 70000` rejected; the accepted form exports `X3M_HDR_TONEMAP=agx`, `LOOK`, `DECODE`, `EV`, `EV_MANUAL` (empty when unset), `CLAMP`; `--hdr` alone exports `X3M_HDR_TONEMAP=identity` |

Final `build/d3d9.dll` SHA-256: `db63e120afcbb38e1382f22dffb96fb6030bbab50d1c3faf2b5587e055ce7e3d`
(seam DLL `c0ae7ea45d23524119fb300e8aadbae05dab2755791395e7e77610227981207a`, fixture `eafb9e6da6b8ab39d29a08dbd5bba2a6ab8bde150fe868005af3946efd96561d`).

Verdict: go for the checkpoint commit of stage 2 with findings 1–3
applied and `--hdr-tonemap` left off by default (`--hdr` alone stays the
stage-1 identity path, bit-for-bit). What the evidence supports: the
compiled AgX program and the meter chain agree with the Python references
on synthetic ramps and blocks to the 8-bit rounding, the host adaptation
matches `simulate` to 1e-6 EV, the ladder and the disable-after-three
behave as designed, the state bracket leaves nothing bound, and the
identity default is untouched. What it does not support: any gameplay
claim (the presented image is an AgX transform of a gamma-space FP16 scene,
LDR to the game's bloom and GUI, never seen in the game), the behaviour of
infinite or NaN pixels on this backend beyond "the host state stays
finite", and the chain's cost at 5120×1440 without the resolve
(observation 9). The first game evidence is the user-run flight with
`--motion-output --taa --telemetry --hdr --hdr-tonemap`: read
`hdr_tonemap` (`tonemap=1 meter=1`), the `hdr_frame` fields `ev`,
`avg_log_l`, `luma_mean`, `dt_ms`, `meter_us`, `readback_us` over a
dark-to-bright transition, any `hdr_unwind=tonemap` or
`hdr_tonemap_disabled` line, and whether the picture reads as exposed
rather than clipped, before any of the switches becomes a default.
