# Review 26: TAA sharpen, mip bias, scene hook default

Independent review of the two merged worktree branches on top of `1924c55`
(merge commits `e51c59a` "Merge mip bias (pre-review 26)" and `47bdfc0`
"Merge post-resolve sharpen (pre-review 26)") plus the scene-hook default
flip made in this review. Three items: (1) the post-resolve RCAS sharpen
(`src/temporal/rcas.hlsl`, `sharpen.h`, `taa_sharpen_ps.hlsl`,
`agx_sharpen_ps.hlsl`, the pass and write-back changes in
`src/renderer/temporal_pass.{h,cpp}` and `hdr_pass.{h,cpp}`, `X3M_TAA_SHARPEN`
in `capture.cpp`, `--taa-sharpen`); (2) the mip LOD bias
(`MotionOutput::apply_mip_bias` / `restore_mip_bias` / `resync_samplers`, the
16-stage sampler shadow, the light `SetTexture`/`SetSamplerState` hooks,
`X3M_TAA_MIP_BIAS`, `--taa-mip-bias`, the capture's `MIPMAPLODBIAS` /
`MAXMIPLEVEL` lines); (3) the engine scene-end hook as the default resolve
point (`scene_hook::wanted()`, `--scene-hook [on|off]`, the runner's
`seam-taa-hook-default` case, the iteration-10 `rs_resyncs` question).
Read in full: the four shader sources and the CPU ABI, the sharpen and
mip-bias diffs of the pass, the write-back and `motion_output.{h,cpp}`,
`scene_hook.{h,cpp}`, the merge-conflict resolutions, the runner's sharpen
and mip-bias validators, the iteration-10 sections 1.4/1.5 and the run-6 log
(queried by script for the 24 frames). No game was launched; every Wine
command ran under `wine_lock.py`, one at a time; result files were queried
by script, never read whole.

## Merge notes

- Mip bias first (`e51c59a`): one source conflict, `check_no_x87.py`
  (`LIGHT_HOOKS` gained `set_texture`/`set_sampler_state` on the branch,
  `GZ_HOOKS` on main): both kept. Capture logs under `verification/results/`
  took the branch side; the suite rerun below regenerates them.
- Sharpen second (`47bdfc0`): both features kept in `capture.cpp` (both
  switches, one `motion_output_mode` line carrying `mip_bias=` and
  `taa_sharpen=`), `motion_output.h`, `tools/manage.py` (both flags, both
  range checks, both env exports), the fixture's `MODE` line (`mipbias`,
  `mip_bias`, `sharpen`), `run_motion_output.py` (both case lists,
  `validate_case`/`finish_case` take `mip_bias` and `sharpen`, every `MODE`
  dict carries both keys) and `temporal-integration.md` (both sections).
  `docs/status.md` keeps main's structure; the two branch bullets that
  auto-merged into older sections were removed and folded into the
  review-26 item of the latest checkpoint.
- Generator `--check` after the merge: nine programs, PASS (the seven old
  hashes unchanged; `taa_sharpen` 418 words `af5cdb6f…`,
  `hdr_tonemap_sharpen` 1691 words `4b36956c…`).

## Checklist

1. **History never sees sharpened pixels** - 8-bit route: the pass draws
   RCAS of `colors_[next]` (the history it just wrote) into
   `in.color_surface` (the game's main target) and never back
   (`temporal_pass.cpp`, the block after the reactive-mask draw); the next
   frame's resolve reads the game's fresh raster, not the display. HDR
   route: the write-back samples the resolved FP16 texture and draws into
   the 8-bit main target; the history texture is only ever bound as a
   source (`hdr_pass.cpp` `copy_draw`). Proof in the suite: `SHARPEN_TWINS`
   (`seam-taa-sharpen-on/half` vs `seam-taa-on`, `seam-taa-hdr-sharpen-on`
   vs `seam-taa-hdr-on`, `seam-taa-hdr-tonemap-sharpen-on` vs
   `seam-taa-hdr-tonemap-on`): the FP16 history readbacks are byte-identical
   to the unsharpened twin's while the presented frames differ (see "Suite
   results").
2. **RCAS divisions and clamps** (`rcas.hlsl`) - noise term
   `max(lumaMax − lumaMin, 1/256)`; `hitMin = mn4 / max(4·mx4, 1/4096)`;
   `hitMax = (1 − mx4) / min(4·mn4 − 4, −1/4096)` (the denominator is
   ≤ 0 for taps in [0, 1], the guard keeps it away from zero); the lobe is
   clamped to `[−0.1875, 0]` (the published limit `0.25 − 1/16`) and
   multiplied by the gain in (0, 1] and `nz` in [0.5, 1], so the resolve's
   denominator `4·lobe + 1` lies in `[0.25, 1]`; the result is clamped to
   `[min(mn4, e), max(mx4, e)]`. No unguarded division remains.
3. **NaN/Inf** - every tap is `saturate`d before use (a NaN tap becomes the
   backend's `saturate(NaN)`, 0 on the verified backend, and cannot widen
   the limiter); the gain is `exp2(−2·(1 − s))` from a finite `s` in [0, 1]
   (`valid_sharpen`, `prepare_sharpen` refuses non-finite or out-of-range
   values and zero dimensions). The CPU side never divides by the switch.
4. **HDR sharpen cost** - +0.80 ms at 5120×1440 (`taa-sharpen.md`: 2.220 →
   3.024 ms), five AgX evaluations per pixel (`hdr_tonemap_sharpen` 1691
   words against 414). The alternatives were not measured: "tonemap once
   into a scratch, then RCAS" needs a display-referred scratch the
   write-back does not have (it draws straight into the game's main target
   from the FP16 texture) plus a second full-screen pass and a target
   switch, so the saving is bounded by four AgX evaluations minus one pass;
   "sharpen luma only" does not remove the taps' tonemaps (the ring's
   display-referred luma still needs AgX per tap), it only trims the
   per-channel limiter. Both change the verified per-pixel contract
   (`RCAS(AgX(x))` within one code) and are the orchestrator's call;
   recorded as a follow-up in "Findings" (4). The 8-bit route is cost
   neutral (2.252 → 2.238 ms: the RCAS draw replaces the copy-back).
5. **`X3M_TAA_SHARPEN` parse and clamp** - `capture.cpp`: the whole string
   must parse (`end != setting && *end == 0`), `0 ≤ v ≤ 1` (NaN fails both
   comparisons), only with `X3M_TAA=1`; `0`, unset or invalid leave
   `taa_sharpen = 0` and the pass is created without the sharpen program
   (`taa_->initialize(..., nullptr)`), so off is shader-for-shader the
   pre-sharpen pass. `manage.py` refuses `--taa-sharpen` without `--taa`
   and outside [0, 1]. The fixture parses the same way.
6. **Mip-bias shadow after the merge** - `restore_bindings()` is the single
   restore point and it ends with `if (sampler_biased_mask_)
   restore_mip_bias()`; its callers cover every unrouted draw
   (`motion_output.cpp:1781` `if (!route.routed) restore_bindings()`, both
   RT modes), `Clear` (`:1559`), `StretchRect` / scene end (`:784`, `:869`
   the hook), `EndScene`, `Present` (`:2293`), `before_reset`, the state
   block hooks (`capture.cpp:1082/1130/1147/1157`: begin, apply, create,
   end), the lazy-mode getters and the final `Release` (`capture.cpp:433`);
   a state block `Apply`/`EndStateBlock`/`Reset` then `resync_samplers()`
   (bindings re-read natively, filters and saved values forgotten).
   Game-side writes: the light `SetSamplerState` hook records an application
   `MIPMAPLODBIAS` write as the new restore value, clears `biased` for that
   stage and counts it (`mip_bias_game_writes`, logged from the next heavy
   call); the static study says the game never issues one. Per-draw cost:
   a routed draw walks the set bits of `bound | biased` (≤ 16, seven in the
   material profiles), one compare per stage once the filter and saved
   value are known; no `GetSamplerState` after the first fill; the light
   `SetTexture` hook adds one pointer compare and a `GetLevelCount` per
   pointer change per stage; an unrouted draw pays one mask test. Nothing
   allocates. Correctness of the restore under both RT modes, after a
   `Reset`, through a state block and with application writes is the
   fixture's `mipbias` script (`taa-mip-bias.md`); rerun below.
7. **Interactions** - sharpen × HDR: `in.sharpen = 0` while `hdr_scene`
   (the write-back sharpens instead, with `HdrConfig::sharpen` from the
   same switch); the write-back's state block now saves c0..c23. Sharpen ×
   lazy RT mode: the sharpen draw runs inside the pass's own state bracket
   after the boundary's `restore_bindings()`. Sharpen × scene hook: the
   resolve site does not matter to the pass (`seam-taa-hook-*` cases with
   `hdr`/`tonemap` are in the suite; a sharpen twin of the hook script is
   not, the display draw is identical code). Mip bias × scene hook:
   `scene_end_hook()` calls `restore_bindings()` first, so the bias is off
   the device before the resolve and the compositor. Mip bias × HDR
   redirect: sampler state is independent of the render-target swap;
   `resync_samplers` runs in `resync_shadow` after a `Reset`. Mip bias ×
   lazy RT mode: shared restore points (`seam-mipbias-lazy-on`,
   `seam-taa-lazy-mipbias-on`). Mip bias × sharpen: none (sampler state at
   draw time vs a post-resolve draw). Identity write-back: `X3M_HDR` without
   the tonemap sharpens with `sharpen_shader_` (the identity+RCAS program),
   verified against `RCAS(clamp(resolved))`.
8. **Scene-hook default, fail-closed paths** - `scene_hook::wanted()`:
   `"1"` on, `"0"` off, unset (or anything else) on iff
   `X3M_MOTION_OUTPUT=1` (no consumer otherwise). `initialize` then still
   requires `object_trace::executable_verified()` (image identity, computed
   independently of `X3M_OBJECT_TRACE`) and the exact five bytes at
   `0x004721b1`; every refusal leaves the bytes untouched and the status
   names it (`disabled`, `executable_mismatch`, `callsite_mismatch`,
   `target_mismatch`, `protect_failed`, `patch_rolled_back`,
   `rollback_failed`). `MotionOutput::configure_scene_hook(active())` is
   false on every one of them, so the bloom-copy `StretchRect` resolves and
   the selector alone decides the scene end, as before. The fixture
   executable exercises `executable_mismatch` in every route case now
   (`seam-taa-hook-default` leaves the switch unset and must equal the
   `hook-on` run through the seam export). Disagreement handling on a
   latch-only screen: `scene_end_hook` returns at "outside the Scene phase"
   without touching the device; the Present cross-check reports `Disagree`
   with `routed=0` (now on the record) and the frame skips the resolve on
   both boundaries (`taa_skip=2`), which is the correct outcome for a frame
   with nothing to resolve.
9. **Performance pass** - no per-draw allocation, QPC or logging was added:
   `sb_resyncs` is one increment per state-block call; the disagreement
   line is per frame at Present and now on its own budget; the sharpen is
   one register upload and one quad per frame; the 8-bit fallback is a
   branch on the failure path only. The mip-bias per-draw work is item 6.
10. **Docs claims backed by numbers** - the status bullet's numbers are the
    `taa-sharpen.md` table (2.252 → 2.238, 2.220 → 3.024 ms, MTF50 0.262 →
    0.298 c/px, 10–90 % rise 1.82 → 1.49 px) and `taa-mip-bias.md`; the
    scene-hook default cites iteration 10 (214 Agree / 24 Disagree / 40
    None, `draws_after_hook` max 0) and iteration 9 run 2 (89/89).

## Findings and fixes

1. **Medium, fixed** - `temporal_pass.cpp` `run()`: the 8-bit sharpen block
   used the `step` lambda, which writes the run's `hr`; a failed
   `SetRenderTarget`/`SetPixelShader`/`SetTexture`/quad of the display draw
   therefore failed the whole run, and "a failed pass never publishes any
   member of a newly written history set" dropped the resolve and the
   history for a display-only draw (the HDR write-back already had a
   redraw-unsharpened fallback). Fixed: the sharpen keeps its own result
   (`Output::sharpen_result`); a lost device still fails the run, any other
   failure leaves the resolve in force with `display_written = false`, the
   caller's `StretchRect` copy-back presents the unsharpened frame, counts
   the failure (`motion_output_sharpen_failed`) and stops requesting the
   sharpen after three (`sharpen_failure_limit`). Not fixture-exercised:
   the 8-bit path has no fault injection (the HDR path's
   `HdrFault::Resolve` does not reach this draw); recorded in
   `temporal-integration.md`.
2. **Low, fixed** - `motion_output.cpp` Present cross-check: the
   `motion_output_scene_hook_disagreement` line consumed `logged_failures_`,
   the budget shared with fill, apply, restore and TAA failure lines
   (`failure_log_limit = 16`). Iteration 10's latch-only transition screen
   produced 16 consecutive disagreements (frames 2,933–2,948), i.e. the
   whole budget, after which no failure line of that session could have
   been written. Fixed: own counter (`hook_disagreements_logged_`), same
   limit; the line now also carries `routed=`.
3. **Low, fixed (answering iteration 10 item 5)** - `rs_resyncs=1` per
   frame on the latch-only screen. The shadow resynchronizes on three paths
   only: state-block `Apply`/`EndStateBlock` (`resync_shadow`), `Reset`, and
   a failed restoration of the route's own state (`invalidate_render_states`,
   always with `restore_failures`); the 24 frames show `restore_failures=0`,
   `apply_failures=0` and no `motion_output_reset`, so each is an application
   state block on that screen (a sprite/font-style save-and-restore, 8–9
   draws per frame). A full re-read after an `Apply` is required (it can
   change every shadowed state) and costs ~14 getters per occurrence, once
   per frame there. The frame line now carries `sb_resyncs` so the next run
   attributes it directly; documented in `iteration-10.md` and
   `live-motion-route.md`.
4. **Follow-up (open, design)** - the HDR sharpen's +0.80 ms at 5120×1440
   (checklist 4). Options for the orchestrator: (a) tonemap once into a
   display-referred scratch (an owned A8R8G8B8 or FP16 target at main
   size), then RCAS into the main target: one extra pass, bounded saving of
   four AgX evaluations per pixel minus the pass; (b) a cheaper ring
   tonemap (luma-only AgX for the four ring taps, full AgX for the centre),
   which changes the limiter inputs and the fixture contract; (c) accept
   the cost on the HDR route (the 8-bit route is neutral). Not measured
   here; measurement needs the scratch and a second draw in `hdr_pass.cpp`.
5. **Observation (open, low)** - `GetSamplerState` (slot 68) is not a
   restore point: an application read of `MIPMAPLODBIAS` between two routed
   draws would return the route's bias. The game's effect state manager
   never reads sampler state back (`sampler-states-and-mips.md`), and the
   lazy-mode getter-hook pattern (`get_rt`, `get_render_state`) is available
   if a future build needs it.
6. **Observation** - the sampler shadow keys the level count on the texture
   pointer (`texture_levels_wanted`: query once per pointer change). A
   texture released and another created at the same address with a
   different level count would keep the old count until the next pointer
   change or resync; the consequence is a missed or inert bias on one stage
   for those draws, never a wrong restore value (that is read from the
   device). Acceptable for a diagnostic switch; the `SetTexture` hook could
   compare the level count on every call at the cost of a `GetLevelCount`
   per bind.
7. **Observation** - `scene_hook` `rollback_failed`: the bytes may remain
   ours with `installed_ = true` and `active()` false; the trampoline then
   still signals and `scene_end_hook()` resolves there (it does not consult
   `scene_hook_installed_`), the Present verdict counts the copy as Agree,
   and `shutdown()` still restores. Consistent; noted for completeness.
8. **Observation** - with the default on, every fixture process that sets
   `X3M_MOTION_OUTPUT=1` and leaves the switch unset attempts the patch and
   logs `scene_hook active=0 status=executable_mismatch` at load and
   `reinstalled=1` once per device creation. `run_motion_output.py` sets
   `X3M_SCENE_HOOK=0` explicitly for every case but `seam-taa-hook-default`
   (its regular cases verify the copy/selector boundary the game gets
   whenever the patch is refused); `run_scene_capture.py`,
   `run_ownership_integration.py` and the object/loading runners never set
   `X3M_MOTION_OUTPUT`, so their processes stay `disabled`. No runner
   assumed "hook off" by default beyond that; no case was renamed.
9. **Observation** - `manage.py --scene-hook` is `nargs='?'` with
   `const='on'`, `choices=['on', 'off']`: `launch --motion-output` requests
   the hook, `--scene-hook` alone means on, `--scene-hook off` disables it;
   the flag must not directly precede the `launch` positional. Without
   `--motion-output` the env is `0`; `--scene-hook` (explicit on) without
   `--motion-output` is still an error.
10. **Observation** - `X3M_TAA_MIP_BIAS` in the DLL requires the jitter
    (`X3M_TAA=1` or `X3M_MOTION_JITTER=1`) while `manage.py` requires
    `--taa`; the jitter-only combination is fixture-only
    (`seam-jitter-mipbias-zero`) and needs no flag.

## Suite results

Clean rebuild first (`cmake --build build --clean-first -j4`): `check_no_x87.py`
on it PASS, 13 roots, 149 reachable, 0 violations. Then the chain, one Wine
runner at a time under `wine_lock.py --holder review26`, bottle `Steam`,
started only after `game_guard.game_running()` had been `[]` for three
consecutive minutes (the user's run 3 was up during the first two attempts;
both refused through `no_game()` and were retried):

| suite | result |
| --- | --- |
| `run_motion_output.py` | PASS: 110 runs exit 0, 94 cases + 26 bench, `{"passed": true}`; the runner's own `--clean-first` rebuild produced the final DLL below (production `8864bff0…`, seam `b34170a9…`). Sharpen twins: `history_identical` for all five (`seam-taa-sharpen-off/on/half`, `seam-taa-hdr-sharpen-on`, `seam-taa-hdr-tonemap-sharpen-on`) while the presented frames differ (8-bit at 1.0: max code error 0.498, 1.2 % of pixels changed, 0 outside the 3×3 bound, alpha intact; AgX: max 0.500, 5.0 % changed, `RCAS(AgX)` told apart from `AgX(RCAS)` on 2–106 pixels per frame). Mip-bias twins identical (16 readback files each: `seam-taa-mipbias-on`, `production-taa-mipbias-on`, `seam-jitter-mipbias-zero`, `seam-taa-lazy-mipbias-on`), `zero_equals_unset`. Hook script: `hook-on` 141 checks `active`, `hook-unpatched` 126 `callsite_mismatch`, the new `hook-default` 141 `active` with the loader's two `status=executable_mismatch` lines. Bench medians at 5120×1440: 8-bit 2.435 → 2.374 ms with the sharpen, HDR AgX 2.415 → 3.039 ms (+0.62 ms; +0.80 on the branch's run); 1280×768: 0.741 → 0.839, 1.298 → 1.865 ms; 4 min wall |
| `run_temporal_pass.py` | PASS: 386 samples, 228 state restorations, 2 device generations |
| `temporal_run.py` | attempt 1 timed out at its fixed 60 s fixture timeout (`timed_out: true`, the machine was still loaded from the game's exit), **rerun PASS**: sources and executable unchanged after the run; the tracked record is the rerun |
| `run_ownership_integration.py` | PASS: 26 runs exit 0, one DLL `f95c8ccf…` (`build-ownership`), build and verification manifests written |
| `run_scene_capture.py` | PASS: 4,908 checks, 16 samples, 36 scenarios |
| `run_loading_trace.py` | PASS: loading-trace 85, loading-mesh 123, mesh-adjacency-cache-off 2,179, cache-on 2,219; verify `calls=37 verify_mismatched=0`, fast `calls=74` |
| `run_object_lifetime.py` | PASS: 574 checks / 80 backend calls |
| `run_object_trace.py` | PASS: 166 checks / 120,017 backend calls |
| `generate_rigid_motion_pixel.py --check` | PASS: nine programs recompiled and equal to the checked-in artifacts (`taa_sharpen` 418 words `af5cdb6f…`, `hdr_tonemap_sharpen` 1691 `4b36956c…`, the seven older hashes unchanged) |
| `check_no_x87.py build/d3d9.dll` | PASS on the pre-chain build and on the final `8864bff0…`: 13 roots (9 light hooks including `set_texture`/`set_sampler_state`, the 4 gz hooks), 149 reachable functions, 0 violations |
| `unittest discover -s verification/analysis` | 716 tests OK (34.7 s) on the tree this commit records. The in-place run in the main checkout reported 4 errors in `test_mesh_adjacency_fast.py` (`generate() got an unexpected keyword argument`): they come from an uncommitted, in-flight edit of `tools/analysis/mesh_adjacency_reference.py` (and `src/proxy/mesh_adjacency_fast.{cpp,h}`, `mesh_adjacency_fast_host.cpp`) by another agent in the main checkout, dated after the chain's DLL build (20:07–20:11 vs 19:54) and outside this review; the verdict above is from a detached scratch worktree of `HEAD` with exactly this commit's files applied and the suite's results linked in |

The `sb_resyncs` field is on every frame line of the rerun (`rs_resyncs=0
sb_resyncs=0` on the regular scripts, where no state block is applied).

Final `build/d3d9.dll` SHA-256:
8864bff00e284db0a23ff152a1cf3e26c0ffaed161d010ec305be4bf46abb532 (the
`--clean-first` relink by `run_motion_output.py` at 19:54 from the committed
sources plus this review's changes, before the foreign mesh-adjacency edits
above; the two pre-chain clean builds of the same sources hashed
`3336a5be…` and `1936a16b…`, the PE timestamp differing as in review 25;
seam fixture DLL `b34170a9…`, ownership DLL `f95c8ccf…`; not installed).

## What `docs/status.md` must say (orchestrator)

- The sharpen and the mip bias are merged and reviewed; both default off;
  the 8-bit sharpen has a copy-back fallback (finding 1, not
  fixture-exercised); the HDR sharpen's +0.80 ms at 5120×1440 is an open
  follow-up (finding 4).
- The scene-end hook is the default resolve point with `--motion-output`
  (`--scene-hook off` restores the copy/selector boundary); the fallback
  chain is unchanged and the fixture exercises the refusal in every route
  case.
- Iteration 10's `rs_resyncs` question is answered (state block per frame
  on the latch-only screen; `sb_resyncs` attributes it from the next run).
- Not installed; the next user-managed run decides the install.
