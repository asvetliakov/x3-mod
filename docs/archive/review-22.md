# Review 22: FP16 HDR scene path, stage 1

Independent review of the uncommitted tree on top of `b37212b`, limited to
stage 1 of the HDR scene path: `src/renderer/hdr_pass.{h,cpp}` (new), the
write-back program (`src/temporal/hdr_writeback_ps.hlsl`,
`hdr_writeback_program{,_inc}.h`), the redirect state machine and the
write-back policy in `motion_output.{h,cpp}`, the `capture.cpp` wiring
(`X3M_HDR`, slots 38/32, the substituted `SetRenderTarget(0)`, the
`ColorFill`/`UpdateSurface`/`EndScene` policies), `telemetry.{h,cpp}`, the
build files, `manage.py --hdr`, the shader generator and its manifest,
`abi_check.cpp`, the fixture, its runner (16 HDR runs, per-pixel dumps) and
seven documents; plus a scope-only look at the stage-2 preparation another
agent left in the tree (`tools/analysis/agx_reference.py`,
`exposure_reference.py`, `src/temporal/agx.{hlsl,h}`, the two analysis
tests, `hdr-scene-path.md` §3). A read-only Opus pass on the same diff sent
seven findings through the coordinator; each is verified below. No game was
launched; one Wine runner at a time; result files were queried with
scripts, never read whole.

## Checklist

1. **References under both ownership models** - the pass keeps the FP16
   texture's level-0 surface only (`hdr_pass.cpp:191-204`: `CreateTexture`,
   `GetSurfaceLevel`, the container dropped; surfaces are not wrapped by the
   ownership layer, `d3d9_ownership.h:52`, so one object is one device
   reference natively and under the wrapper) and the write-back shader
   (`:527-537`, dropped on every refused gate); both are in
   `device_references()` (`motion_output.cpp:243`) and go in
   `release_resources` (`:257-264`) before the final Release probe
   (`capture.cpp:401-407`). The held main surface is the native
   `GetRenderTarget(0)` result (`:1806-1812`, one reference), released at
   every end (`end_redirect`, `:1870-1878`), on Reset and release
   (`drop_redirect`, `:1879-1884`), and deliberately not counted in
   `device_references()`: the redirect is Off at every frame end, so the
   probe can only see it on a mid-frame device release (observation 9).
   `hdr_logical_render_target` (`:1757-1762`) AddRefs the held pointer,
   which is the application's own pointer (no surface wrapping; the fixture
   compares it with `back.p`). Every fixture environment reaches zero
   references at teardown (the `require` at the end of `main`), including
   the new Reset-while-active step (finding 1). Reset order: the rebind of
   the main surface (finding 1), the drop, `release_target`,
   `HdrPass::before_reset` (`:1031-1042`), all before the native Reset;
   nothing is recreated on a failed Reset (the target is lazy, `ensure_target`
   at the next latch; `hdr_target_failed_` and the block are cleared in
   `before_reset` only). Device lost: `write_back` reports `lost`, skips the
   `StretchRect` rung (`hdr_pass.cpp:574-575`), still attempts the rebind and
   closes its own scene (finding 5); the frame is blocked until a recheck
   or the Reset.
2. **Logical-binding shim** - every hooked device method that reads or
   writes RT0 against the policy: `SetRenderTarget(0)` substituted
   (`capture.cpp:656`, `before_set_render_target` `:1746-1756`: main →
   target, other → verbatim after a flush, committed only on `SUCCEEDED`
   in `after_set_render_target`); `GetRenderTarget(0)` answered with the
   held main while Active (`:1039`), forwarded while Suspended or Off;
   `GetRenderTargetData(main)` flushes first (`:1049`, `:1763-1765`);
   `StretchRect` in `before_stretch` (`motion_output.cpp:589-593`): the
   bloom copy ends, a copy writing main ends (`ContentWrite`), a copy
   reading main flushes, all before the native call and before the TAA
   resolve; `ColorFill`/`UpdateSurface` on main end first (`capture.cpp:775,
   794`); `Clear` latches or marks dirty (`:1333-1338`); `EndScene` flushes
   (`:714`, `:1769`); Present ends (`:2022`); state blocks carry no targets
   and `resync_shadow` maps a physical FP16 RT0 back to the held main
   (`:1228-1240`); `UpdateTexture`, `GetFrontBufferData` and `Lock` cannot
   touch the back buffer (design note, unchanged). Compositor sequence with
   the hook off: `get_rt` → held main, then the bloom `StretchRect(main →
   sceneMap)` reaches `before_stretch` → `end_redirect(BloomCopy)` →
   write-back and RT0 = main → the native copy reads the written-back
   image (fixture: all TAA twins end at `bloom_copy`, colour hashes before
   the boundary identical). Hook on: `scene_end_hook` ends before the
   resolve and before the trampoline returns to the compositor
   (`:660`; hook script frames end at `hook`, the outside-Scene frame 2
   at `bloom_copy`). Environment-map excursion: `SetRenderTarget(0, face)`
   flushes and suspends, draws while Suspended mark nothing dirty (`:1545`
   is Active-only) and land natively in the face, a Clear while Suspended
   clears the face, `GetRenderTarget(0)` forwards, the rebind of main
   substitutes the target with its content intact (`envmap-hdr`
   `suspended=1 resumed=1` in frame 1, `hdrvalues` frame 3 values hold).
   `hdr_pending_state_` is written before the native call and consumed in
   `after_set_render_target` under the same lock; no hook re-enters
   between the two.
3. **Must-unwind ladder** - after a redirect every failure path ends with
   RT0 = the final binding: `copy_draw` restores through `restore(saved,
   final_rt0)` even after a failed draw (`hdr_pass.cpp:315`), a failed
   restoration or `StretchRect` falls to the explicit `bind`
   (`:585`), and one `hdr_unwind=` line per unwind (`hdr_logged_` capped
   by `failure_log_limit`, `motion_output.cpp:1857-1863`). The residual
   ("both copy rungs fail on a non-lost device → one undefined frame under
   DISCARD") stands; the cheap mitigation implemented here is to prove
   the second rung at attach rather than trust the cap (finding 8): the
   self test now fills the 4×4 A8R8G8B8 target black and runs
   `StretchRect(FP16 → A8R8G8B8, POINT)` (`:462-478`), demoting the rung
   when it fails so the ladder never calls a copy known not to work. Both
   rungs are therefore verified where they exist; a runtime double failure
   is then a device fault of the frame, not an untested path.
4. **Bit-identity** - the deviation is only on the material class: the
   per-pixel comparison (`run_motion_output.py:867-899`) classifies every
   twin pixel by the non-HDR value and the acceptance (`:902-909`) requires
   background and flat exact, alpha exact, ≤ 1 code, ≥ 98% exact; measured
   489 / 27,416 material pixels (1.8%) in the four plain twins, 526 / 550 /
   399 / 210 in the TAA, hook and envmap twins, channels `b/g/r/a =
   70/171/253/0`. A 1-code offset on all material pixels (56% of the
   frame) would already fail the 98% bound, but a partial systematic
   offset could hide inside 2%; two additions close that (finding 7): the
   material class is bounded to 10% of itself with the FP16 prediction
   written down (half an ulp over a code width, `2^-12 / (1/255)` = 6.2% per
   channel at most), and the value script now draws the in-range
   `(0.75, 0.25, 0.375, 0.625)` (exact in FP16, codes 191.25 / 63.75 /
   95.625 / 159.375) and requires the presented DWORD `9fbf4060` exactly on
   every B pixel (`motion_output_fixture.cpp:1192-1246`, frames 1 and 4:
   171 and 77 pixels, 0 mismatches). A bias or a sampling offset cannot
   pass that; the residual one-code class is the double rounding of
   unquantized outputs. Docs state the fact plainly (README, `manage.py`
   help, `hdr-scene-path.md`, the verification record).
5. **Self test and caps gate** - fail-closed order in `HdrPass::attach`
   (`hdr_pass.cpp:495-538`): device/native, `ps_3_0`, `NumSimultaneousRTs`,
   `MRTINDEPENDENTBITDEPTHS`, factory, the four `CheckDeviceFormat`s, the
   shader, the self test; the factory reference is dropped on every branch,
   the shader on every refusal, the self test's eleven objects in one
   tail (`:482-484`) after any `break`. No hot-path cost with HDR off: the
   per-draw check is one enum compare (`motion_output.cpp:1545`), the
   `set_rt` shim returns at `index != 0 || Off` (`:1748`), slots 38/32 are
   hooked only with the switch on (`capture.cpp:1179`), `before_clear`
   tests one cached bool (`:1335`). With HDR on, per draw is the same
   compare; the redirect adds one native `GetRenderTarget` and one bind per
   latch (`redirect_us` 1–6 µs) and the write-back its state save/restore
   plus one quad.
6. **Telemetry, x87, manage.py** - `MotionHdrCounters` is part of the
   per-frame `counters_` (zeroed in `begin_frame`), `unwind_reason` points at
   string literals, `hdr_logged_` bounds the failure lines; the six metrics
   are in the `names` table (`static_assert` on `Metric::Count`).
   `check_no_x87.py build/d3d9.dll`: PASS, 129 reachable functions, 0
   violations; the new hooks (`get_rt`, `get_rt_data`) and the extended ones
   all run under `CpuCallBoundary` (FNSAVE/FRSTOR, `cpu_state.h:14-19`), so
   none needs to be a light-hook root; the light `set_render_state` and
   `set_viewport` roots reach no HDR code (the flush is only called from
   heavy hooks). `manage.py`: `--hdr` alone → `--hdr requires
   --motion-output.`; `--dry-run --motion-output --hdr` → `X3M_HDR=1`,
   default `0`; `capture.cpp:1254` parses the exact one-character value.
7. **Docs** - README ("no tonemapping, exposure or HDR output yet"),
   `manage.py` help, `live-motion-route.md`, `telemetry.md`,
   `platform-portability.md`, `motion-output.md` and both HDR documents
   describe an identity path with the one-code deviation; no claim of
   visible or tonemapped HDR anywhere (`docs/status.md` not edited by this
   review). Stage-2 preparation: `agx.{hlsl,h}`, the two reference modules
   and their tests are referenced by nothing in `CMakeLists.txt`,
   `build_motion_output.sh`, the runner build lists or any `src/` include
   (grep: 0 hits outside the files themselves); `hdr-scene-path.md` §3
   states the same; the 560 analysis tests (including
   `test_agx_reference.py` and `test_exposure_reference.py`) pass.

## Findings and fixes

1. **Medium, fixed** (Opus 1, confirmed as a hazard; its failure claim not
   reproduced) - `drop_redirect()` released the held main surface and the
   FP16 target without rebinding RT0, so a Reset issued while Active left
   the device's RT0 pointing at our released texture, and the
   application's own pre-Reset `SetRenderTarget(0, main)` would have been
   substituted back to the target by `before_set_render_target`. On this
   backend a Reset in that state was never exercised by the fixture (every
   Reset followed a Present); whether it returns `D3DERR_INVALIDCALL` is
   backend-dependent (wined3d unbinds the state before it enumerates
   default-pool resources), so the "Reset fails / device never reaches
   zero" part is not confirmed here. Fixed regardless: `before_reset` binds
   the held main back to RT0 before the drop (`motion_output.cpp:1039`,
   viewport and scissor preserved; `release_resources` keeps the plain
   drop, since its device may be mid-destruction). The value script now
   latches a frame, draws, ends the scene and Resets while Active
   (`motion_output_fixture.cpp:1266-1280`): `GetRenderTarget(0)` reports
   main before it, the Reset returns `00000000`, a third `hdr_target` line
   follows and the teardown reaches zero references.
2. **Medium, fixed** (Opus 2, confirmed; found independently) - a failed
   latching Clear reached `end_redirect(ClearFailed)` with `hdr_dirty_`
   already true (set at the latch), so the never-cleared FP16 texture was
   written back over the main target. Fixed: the dirty flag is cleared
   before the end (`:1356`), which turns the end into a rebind only. The
   fixture cannot make a real `Clear(0, NULL, TARGET|ZBUFFER, …, 1, 0)`
   fail on this backend (a mismatched depth size is silently accepted), so
   the seam gained fault kind 10 consumed in `after_clear` (`:1354`); the
   fault script's frame 14 shows `end=clear_failed writebacks=0 flushes=0
   writeback_source=none` with the frame continuing LDR.
3. **Low, fixed** (Opus 3, confirmed as a portability hazard) - `copy_draw`
   and `mrt_draw` bound RT0 before unbinding RT1.., so a 4×4 self-test RT0
   (the mid-frame recheck) or a copy destination of another size could
   coexist with a larger application RT1 between two calls. Not observed on
   wined3d (no cross-target size check at bind time; the lazy-mode RT1/RT2
   are restored before every heavy hook anyway) but D3D9 requires equal
   dimensions across bound targets. Fixed: RT1.. are unbound first, then
   RT0, then the set's own RT1/RT2 (`hdr_pass.cpp:295, 335-337`).
4. **Low, fixed** (Opus 4, confirmed latent) - the injected fault counters
   were consumed at the top of `write_back`, i.e. also by rebind-only calls
   (`write == false`); with the fixture's single-shot faults no case was
   affected. Fixed: computed inside the copy branch (`:552-553`).
5. **Low, fixed** (Opus 5, confirmed) - a scene bracket the pass opened
   itself (only the Present end with content pending) was not closed when
   the draw reported a lost device; the runtime's in-scene flag would then
   refuse the application's next `BeginScene`. Fixed: `EndScene` is called
   whenever `own_scene` (`:565`).
6. **Low, fixed** (Opus 6, confirmed) - the `CheckDeviceFormatConversion`
   query used a hard-coded `A8R8G8B8`. Fixed: `attach` reads the back
   buffer's format through the native `GetRenderTarget(0)` + `GetDesc`
   (`motion_output.cpp:833-837`, fallback `A8R8G8B8`) and logs it
   (`main_format=21` in every run; the runner asserts it).
7. **Low, fixed** (this review; Opus item "trivial" 7 confirmed) - the
   runner's twin tolerance could mask a partial systematic offset inside the
   2% budget and the fixture had no exact-code check of an in-range value
   (checklist 4). Fixed as described there: the material bound
   (`run_motion_output.py:857-864, 902-911`), the mid-value frames, the
   `mid=` field, and the self-test string now includes `stretch_errors=0`.
   `HdrPass::target_failed_` (written, never read; `MotionOutput` keeps its
   own latch) removed.
8. **Observation, implemented** - the self test's fourth step proves the
   `StretchRect` rung (checklist 3); `hdr_device` lines gain `stretch=` and
   `stretch_errors=`; the detail buffers grew to 320 bytes.
9. **Observation** - `hdr_main_` is not in `device_references()`. The
   release probe (`capture.cpp:401`) runs when the application releases the
   device; with the redirect Off at every frame end the held reference
   exists only between a latch and its end, so a mid-frame final Release
   would see one reference more than `held` and skip `release_resources`
   (the application's count would then be off by the route's objects until
   the destructor). Not reachable from the game's frame loop; noted.
10. **Observation** - `end_redirect` while Suspended releases the main
    surface without a bind: the application bound the other surface itself
    and the main target holds the flush of the switch. Correct, and the
    envmap twin's frame 1 (`writebacks=2`) shows it. A `ContentWrite` end
    while Suspended leaves the frame LDR after the application's rebind of
    main (forwarded verbatim in Off), by design.
11. **Observation** - the write-back copies the whole surface with the
    scissor test, clip planes and stencil off and the application's
    viewport restored afterwards; the twins prove the identity for a
    64×64 frame and the bench sizes only through timing. The same
    normalization list is the one the temporal pass uses.
12. **Observation** - the Opus items reported clean (GetContainer/drop
    balance, `hdr_logical_render_target` AddRef, `SavedState` releases,
    commit-on-success of the state machine, `hdr_is_main` identity,
    Present/EndScene order, guarded fixture entry points) were re-read and
    agree with this review.

## Results after the fixes

| Suite | Result |
| --- | --- |
| `cmake --build build --clean-first -j4` (by `run_motion_output.py`) | OK (RelWithDebInfo, `-Wall -Wextra`, 0 warnings); the seam DLL and fixture with `-Werror`, 0 warnings; no later runner relinked `build/d3d9.dll` (hash checked after every suite) |
| `python3 -m unittest discover -s verification/analysis` | 560 tests OK (stage-2 reference tests included) |
| `check_no_x87.py build/d3d9.dll` | PASS: 129 reachable functions, 0 violations |
| `run_motion_output.py` (full, three passes; the last is the recorded one) | PASS: 63 runs (55 cases + 8 bench), all exit 0; HDR twins 48,663 / 49,152 exact (99.01%) in the four plain twins, 48,626 / 48,602 (TAA), 28,273 / 28,672 (hook), 20,270 / 20,480 (envmap), max 1 code, background / flat / alpha 0 differing, material 489 / 526 / 550 / 399 / 210; `seam-hdr-values` 26 checks, 6 frames, 2 Resets, mid frames 1 and 4 exact `9fbf4060`, max FP16 error 3.8e-6; `seam-hdr-fault` 119 checks, 15 frames, unwinds `draw/stretch, restore/shader, stretch/restore, lost/restore, draw/stretch`, rechecks 2–5 pass / 12 fail, frame 14 `clear_failed` with 0 write-backs; caps-absent / self-test-absent identical to `seam-on`; every `hdr_device` `stretch=00000000 stretch_errors=0 main_format=21`; bench boundary medians 0.415 / 0.779 ms at 1280×768 and 0.927 / 2.561 ms at 5120×1440 with HDR (0.364 / 0.738 and 0.492 / 2.272 without) |
| `run_temporal_pass.py` | PASS: 416 numerical / 164 state checks, 2 generations, 386 samples; camera drift (px) static 0.069, yaw 0.124, pitch 0.176, yaw unjittered 0.062, narrow 0.026, single step 0.033 / 0.029, identity control 0.862, swapped control 1.051, after the cut 0.100 |
| `temporal_run.py` | PASS: 78/78 sample checks, reset passed, 2 device generations |
| `run_ownership_integration.py` | PASS: 26 runs exit 0, build report `PASS`, 29 per-mode reports with 0 failures (contracts 370 checks) |
| `run_scene_capture.py` | PASS: 4,908 checks, 16 samples, 36 scenarios, sources and executable unchanged during the run |
| `generate_rigid_motion_pixel.py --check` | PASS: four shaders recompiled and equal to the checked-in artifacts (`hdr_writeback` 46 words, bytecode `9cbb62f6…`) |
| `manage.py --dry-run` | the rejection and the two accepted forms as in checklist 6 |

Final `build/d3d9.dll` SHA-256:
`4f46feee7d9204bd7fbae55378d6e15fb0c2bfe81fddb7e4c64fa0ca84388a8d`
(seam DLL `40345231c2f388df3a58638ec4202ead47b8304b6e0de86b9b829c2c921a2f17`,
fixture `e46e4a4a6f7f815dfdbc0cfd0ff4a44655860be6657974230ecf0a3a95fb3017`).

Verdict: go for the checkpoint commit of stage 1 with findings 1–8
applied and `--hdr` left off by default. Stage 1 is an identity path: the
presented picture is the non-HDR picture to within one 8-bit code on
unquantized material values, and nothing about tonemapping, exposure or
HDR output is claimed. The first game evidence is a user-run flight with
`--motion-output --taa --telemetry --hdr` (with and without
`--scene-hook`): read `hdr_device` (`reason=ok`, `main_format`), the
`hdr_frame` end distribution (`hook` / `bloom_copy` in normal frames,
`present` only with glow off and no hook, `dirty_at_present=0`), any
`hdr_unwind=` / `hdr_recheck` line, `suspended`/`resumed` around the
environment-map frames, and the double-cursor check after alt-tab, before
treating the redirect as confirmed in gameplay.
