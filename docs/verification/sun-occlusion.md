# Partial sun occlusion: verification ledger

Design: [sun-partial-occlusion.md](../architecture/sun-partial-occlusion.md). Sites, ABI and record
layout: [lens-flare-visibility.md](../reverse-engineering/lens-flare-visibility.md) sections 11-16.
Append outcomes here.

## 2026-09-22: step 1 implemented, not installed, not flown

Default off (`--sun-occlusion` / `X3M_SUN_OCCLUSION=1`); nothing is patched and the draw hooks pay
one flag test when neither it nor `X3M_SUN_OCCLUSION_LOG=1` is set. Bottle X3, CrossOver Preview.
No game launch. Nothing below is evidence about the picture in flight.

| Check | Command | Result |
| --- | --- | --- |
| Static sites on the installed EXE | `/usr/bin/python3 verification/probe/verify_sun_occlusion_sites.py` | 16 / 16: SHA-256, 21 context bytes per site, both rel32 targets, the probe's 148-byte prologue (FNV-1a `0x0e5f40888a926996`) and its decoded gate shape, the `xor eax,eax` exit of the rect test, the traversal's entry, callers exactly `{0x471630}` and `{0x4722b5, 0x472491, 0x47e8f6}`, no branch into either call's interior, no absolute reference, one overlapping claim (`submit_phase_sort_return_b`) |
| Host: decision, readiness, footprint, blend classes, main-view walk, pixel wrap, launcher, sites | `/usr/bin/python3 verification/probe/run_host_suite.py --modules test_sun_occlusion` | 14 tests, 0 failing. The gate table is compared with a Python restatement of the vanilla prologue over 648 input combinations, including "outside the rect with `0x8000000` set = 0" |
| Neighbouring host modules | `... --modules test_check_no_x87 test_submit_phases test_collide_memo test_cull_census test_sun_occlusion` and `... --modules test_object_capture test_sun_share_lane` | 53 tests / 0 failing; 15 tests / 0 failing (the host doubles that include `motion_output.h` compile) |
| Hook fixture (synthetic sites with the engine's shapes) and GPU fixture (detached pass) | `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_sun_occlusion.py` | passed: hook 52 checks / 0 failures, GPU 86 checks / 0 failures; record `verification/results/bottle-X3/sun-occlusion.json` |
| Production scratch build | `cmake -S . -B <scratch> -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPython3_EXECUTABLE=/usr/bin/python3 && cmake --build <scratch> -j` | exit 0, 0 warnings |
| x87 audit of that DLL | `python3 verification/probe/check_no_x87.py <scratch>/d3d9.dll` | 563 reachable functions, 0 violations; five new roots (both thunks, the probe handler, begin, end) |
| Launcher | `tools/manage.py launch --dry-run --ownership --object-trace --object-lifetime --motion-output --taa --hdr --sun-occlusion --sun-occlusion-log --sun-occlusion-radius 1.5` | exit 0; env gains `X3M_SUN_OCCLUSION=1`, `X3M_SUN_OCCLUSION_LOG=1`, `X3M_SUN_OCCLUSION_RADIUS=1.5000` |

Hook fixture, what the 52 checks cover: redirect bytes; the original reached by `jmp` on the
caller's exact frame (its return address is the site + 5) with both answers; override answers
without the original running; EBX / EBP (a value) / ESI / EDI, ESP and the two stack arguments
intact after every call; two live x87 stack entries, a non-default MXCSR and LastError intact across
the probe thunk, and across the lens thunk with a listener that runs x87 code, rewrites MXCSR and
sets LastError; bracket order begin < original < end with the flag set only inside; the traversal's
argument still on the caller's stack afterwards; no bracket before the first probe; gates 1 and 3
hide, gate 2 precedes gate 3; foreign view, foreign record, second sun, failed pass, blocked,
device Reset, a frame gap and an unknown main view all reach the original; `present()` closes a
bracket left open; a second thread goes to the original and is counted; log mode runs the original
exactly once per probe and logs `vanilla=` / `answer=`; observe-only never answers; second-claim
failure restores the first site; `shutdown()` restores both; a late claim is refused with the bytes
untouched.

GPU fixture, measured on X3 (320 x 180 RT2, disc radius 0.1 u, 32 taps):

| Scene | open / valid taps (CPU twin) | raw | used |
| --- | --- | --- | --- |
| open | 32 / 32 | 1.0 | 1.0 (exact) |
| covered | 0 / 32 | 0.0 | 0.0 (exact) |
| edge through the centre | 16 / 32 | 0.5 | 0.5 |
| edge at +r/2 (area 0.8045) | 25 / 32 | 0.78125 | 0.79883 |
| edge at -r/2 (area 0.1955) | 7 / 32 | 0.21875 | 0.20068 |
| centre on the screen edge, open / covered | 16 / 16, 0 / 16 | 1.0 / 0.0 | 1.0 / 0.0 |

No valid tap: a seed gives 1, a smoothed frame keeps the history. Radius 0 (a record's first
frame) is refused untouched and the fallback radius runs. Step response at weight 0.25 from 1
towards 0: 0.75, 0.5625, 0.42188, 0.31641, 0.2373, 0.17798, 0.13342, 0.10004 (expected (3/4)^n;
the last two differ by FP16 storage), then exact 0 in under 20 frames; the rise reaches used = 1.0
exactly within 13 frames at smoothed 0.976 (the dead band absorbs the FP16 stall). A32B32G32R32F
and G32R32F RT2 accepted, A8R8G8B8 refused; a failed draw restores, invalidates, makes lens draws
`not_ready` and the next frame seeds by itself. Lens draws at fraction 0.5: ps_2_0 and ps_3_0
originals under SRCALPHA/INVSRCALPHA, SRCALPHA/ONE, ONE/ONE, ONE/INVSRCCOLOR and
ONE/INVSRCALPHA match `f x` the reference within 2.5 / 255 (10 draws), unwrapped draws match the
plain law; wraps are created once per program and blend class (no creation over five repeated
draws); refusals (blend off, other law, sRGB write, alpha test at ref 128, no pixel shader, no
hash, ps_1_1, a failed bind) change nothing. Every transaction is bracketed by a byte comparison of
RT0-3, depth, viewport, scissor, shaders, declaration / FVF, stream 0, indices, 16 + 4 textures, 13
sampler states on 16 samplers, 31 render states, two stage states and c0-c3 under hostile state.
Native Reset succeeds after `before_reset()` (six objects released, programs and wraps kept) and
the next frame seeds and matches the twin; `detach()` leaves 0 references. Program: 469 ps_3_0
slots of 512.

Not verified: anything in flight (which programs the lens draws use, whether every one wraps,
whether RT2 holds the sentinel where the sun shows, the derived radius against the visible disc,
the look of the fade); native Windows execution; the motion-route fixture DLLs were not rebuilt
here (queue owner); cost in the game (the counts are: per frame one probe handler call per probe,
one pass of roughly 110 device calls (the state normalisation of the sibling passes), per lens draw about 30 device calls; none timed).

## 2026-09-22: review fixes on merged main (f61a3bd7), still not installed, not flown

Deep review of `cb8267f9`: hook / ABI side sound; six findings fixed.

1. A refused lens draw, and the rest of its bracket, is dropped in the frame that sets the block (the
   fraction is GPU-only, so "f = 1" cannot be known); the block is sticky for the process, so a Reset
   cannot replay that frame. Transient refusals (shadow state unknown, device error) cost the frame and
   the next without blocking.
2. `classify_blend` refuses `D3DRS_FOGENABLE`; the fingerprint logs `fog=` (and `srgbwrite=`, `alphafunc=`).
3. The probe handler runs inside a LastError envelope; both thunks save EFLAGS and clear DF around the C
   handlers. The hook fixture links the production `engine_memory.cpp` and drives the production
   main-view walk (VirtualQuery refuses the unmapped engine globals) under hostile LastError / MXCSR / x87,
   and runs override path, original path and bracket at every 4-byte alignment of ESP (pads 0, 4, 8, 12)
   with DF clear and set: handlers see DF = 0, the site gets DF back.
4. A pass skipped untouched holds the last fraction for at most four consecutive frames (`core::Hold`,
   host-tested); failures and exhausted holds drop the frame as before.
5. Device calls, measured by the GPU fixture: visibility pass **33 per frame (was about 110)**, wrapped
   lens draw **5 (was about 30)**. The per-frame setters became one recorded block; `D3DSBT_ALL` is gone;
   per-draw state comes from the route's shadow and a recorded per-sampler block that also holds the
   application's shader reference. No device getter runs per lens draw.
6. One `sun_occlusion_foreign_thread` line when a probe arrives from a thread other than the fixed owner.

| Check | Result |
| --- | --- |
| `git merge main` (f61a3bd7) | clean, no conflicts |
| `/usr/bin/python3 verification/probe/run_host_suite.py --modules test_sun_occlusion` | 15 tests, 0 failing |
| `... --modules test_check_no_x87 test_submit_phases test_collide_memo test_cull_census test_object_capture test_sun_share_lane` | 7 modules, 69 tests, 0 failing |
| `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_sun_occlusion.py` | hook 64 checks / 0 failures, GPU 89 checks / 0 failures, `CALLS execute=33 lens_draw=5` |
| scratch build + `check_no_x87.py` | exit 0, 0 warnings; 565 reachable functions, 0 violations |

Still unverified: everything in flight, native Windows, the hold and drop paths inside `MotionOutput`
(they sit behind the route and have no fixture of their own; `core::Hold` and the pass are tested), cost
in the game.


## 2026-09-22: Run 223 triage (first flight, Run63 DLL b0cde491 from 5112bb43): the override never touched the sun

Flight `/tmp/x3-bottleX3-run223`, session A with `--sun-occlusion --sun-occlusion-log --taa-debug`, HDR, TAA, sun
shadow lane. Three F8 bursts: 18727-18758 (clear), 19818-19849 (about half covered), 20558-20589 ("just fully
covered"). Compact record: `verification/results/run223-sun-occlusion-triage.json`. Diagnosis only; nothing changed.

**A. The override was installed and never active for the sun (measured).** `sun_occlusion patched=1 reason=ok`,
`sun_occlusion_device attached=1`, no `sun_occlusion_blocked`, `_failed` or `_foreign_thread` line in the session.
All 115,019 `sun_probe` lines of the sun records (groups 1 and 5, 103,364 lines) carry `own=0 ready=0`: the sun
record is owned by view `334fe888` (layer 15, `+0x270 = 0x00400135`, the `0x400000` background/nebula-star regime of
camera-state-and-frame-routine.md), while the main view resolved through `cockpit+0x58` is `334fe0f0` (layer 16,
`0x0085492d`). `decide()` requires `record_owner == view == main_view`, which the sun record never satisfies. Each
frame the record is probed eight times (layers 15, 16, 20, 30, 61, 100, 195, 200); the **only probe that ever hid it
is the main view's cross-view re-probe** (`layer=16 vanilla=1`: 1872 of 1872 sun hidden verdicts; the owner's own
probe answered 0 in every frame, the later views always 0). RE note section 14 expected the rect test to reject
cross-view probes; for the background view and the main view the rects and cameras coincide, so the cross probe is
the operative one. Consequences in all 96 burst frames: `sun_visibility skip=record single=0 answered=0`, every
`sun_lens_draw verdict=not_ready`, `sun_lens_bracket wrapped=0 dropped=0`. Both reported defects are therefore
vanilla behaviour observed with the feature idle, not a fade artefact. `f_raw` for the sun does not exist (the
pass never ran for it), so body-31 self-occlusion is unanswered by the log; the HDR dump has no emission at the
record position (mean 0.08 over r = 24 px against a sky reference of 0.22), so in this sector no in-scene card is
visible there and the "sun" the user sees is the lens chain alone.

The override did answer later in the session (frames 25798-34292) for `group=25` records **owned by the main view**
(non-TSuns lens sources, up to three per frame, sizes 5,779-18,594): 49 `ready=1` probes, 25 frames with `wrapped=2`
(`verdict=applied`, ps `8360f422de08b5bd` ps_2_0, ONE/ONE -> rgb scale, one variant), and **24 frames with `drop=1`**
where the first record was answered and a second record appeared later in the same frame (`skip=record`), so the
whole lens bracket was dropped for that frame: a one-frame flicker of every lens flare on screen, the design's
"second sun" case triggered by ordinary ship flares. Not reported by the user, real.

**B. Where the chain vanished (measured from RT2 `depth_1_*.rgba32f` .r, sentinel -1 = open).** Record uv from
`0.5 + x/65536, 0.5 - y/65536` (consistent with the frame's projection `m00 = 0.8 = cot/scale_x`, `m11 = 1.333`,
`scale_x = 81920`, fov 16384; not checked against a picture of the chain, which no dump contains, see C). Burst 1:
centre (595, 270), open fraction 1.000 within r = 32 px in all 32 frames. Burst 2: centre (585, 270) open in all 32
frames, open fraction 0.589-0.618 within r = 32 (0.56 within r = 64); the vanilla probe answered visible, `acc=200`,
10 draws at full strength. Burst 3: same record position (`x=-2802 y=9663`, the ship did not turn; the station edge
moved), centre covered in all 32 frames, open fraction 0.508-0.523 within r = 32; `layer=16 vanilla=1`, `acc=0
size=0`, no lens draws (chain destroyed by the ramp). Hidden intervals of the sun record over the session:
8965-9202, 12605-12909, 13247-13374, 20095-20260, 20398-20773 (contains burst 3), 21588-21689, 21855-21940,
24812-24817, 32339-. So the pop happens exactly when the swept probe at the disc centre hits (about 50 % of a
32-64 px disc covered), as RE note section 4 describes; nothing else hid it (no rect refusal, no `0x8000000`, no
readiness gap: the override was simply not eligible).

**C. Why the chain draws over the station (measured fingerprint).** Group 5 issues 10 draws per frame, all
`vs d5e1c75351ed3f04 / ps 8360f422de08b5bd` (ps_2_0), `ALPHABLENDENABLE=1 SRCBLEND=ONE DESTBLEND=ONE op=ADD`,
**`ZENABLE=0`**, `ZWRITEENABLE=0`, alpha test off, fog off, colour write 0xf, RT0 the 1280x768 X8R8G8B8 back buffer:
six octagon fans (8 triangles / 9 vertices; stage-0 textures `5d01d640` 1024^2 X8R8G8B8 at lens indices 0, 1, 6 and
`5d01d500` 1024^2 DXT1 at 5, 7, 9) and four quads (2 triangles; `5d01d780` at 2, 3, `5d01d6e0` at 4, `5d01d5a0`
512^2 DXT1 at 8). Additive, no depth test, drawn after the write-back: whatever the probe leaves alive paints over
the station at full strength until the centre is covered. That is vanilla, and it is what the user saw; no draw was
scaled in the bursts (`wrapped=0`), so "half strength" never happened and a brightness comparison at f = 0.5 is
not possible from this flight. The `color_1` / `present_1` / `hdr_1` readbacks all precede the lens block
(`0x004721b1` scene end, then write-back, then `0x00472491`), so no dump shows the chain; a Present-time back-buffer
readback is needed before any look can be judged from files.

**D. Recommendations.**

1. *Step 1 fix, blocking:* the override must act on the **main view's probe of a record owned by a background-regime
   view** (`view == main_view`, `record+0x8 != view`, `owner+0x270 & 0x400000`; log the owner's layer and flags),
   and must leave records owned by the main view (group 25 and any other non-sun source) to the original
   **without counting them** in `Ready::records`. This also removes the 24 dropped-bracket frames. The owner's own
   probe and the later views' re-probes keep the original (all returned 0 in this flight; keep watching
   `vanilla=1` at layers other than 16). Host test, hook fixture ("foreign record reaches the original" inverts for
   this case) and the design note's "Main view only" paragraph change with it.
2. *Step 1 fix, blocking:* `record+0x34` is **saturated at 0xffff x acc/200 for the sun** (86,174 of 86,174 frames
   at acc 200, 32,767 at acc 100, both suns), so `core::footprint` would derive 0.4 u and clamp to the 0.25 u cap
   (320 px half-width): `f` would fall long before the disc is touched. Treat `size >= 0xffff * acc / 200` as
   unknown and use an explicit radius (`--sun-occlusion-radius` as an absolute fraction of the width, default
   about 0.03-0.05 u, i.e. 40-60 px at 1280; calibrate from the Present-time readback of the next flight).
3. *Diagnostic for the next flight:* a back-buffer readback at Present for burst frames (`lens_1_N.bgra8`) so the
   chain is in the files; `sun_probe` with the owner's layer and flags; the sun lane's direction projected with the
   scene camera beside the record uv.
4. *Step 2, now that the draws are fingerprinted:* with one VS/PS pair for the whole chain and no `vPos` in ps_2_0,
   per-pixel clipping needs a structural **vertex-shader wrap** too: redirect `oPos` to a temporary, then write it
   to `oPos` and to a free output texcoord, and also emit the body's transformed origin (the sprite centre) in a
   second free texcoord. The PS wrap then (a) computes the fragment's back-buffer uv from the first texcoord,
   samples RT2 at 1-5 taps (soft edge over about 2 px) and forms `open_px`; (b) classifies the body from the second
   texcoord against the latched sun uv: centre within about 2 px of the sun -> factor-1.0 body (core glow, rays,
   streaks, cards): `out *= open_px * f`; centre collinear with the sun and the screen centre -> ghost: `out *= f`;
   otherwise (another record's body) untouched, which also stops the sun's `f` from scaling ship flares. Ghosts
   never clip. Cost: two texcoords per vertex on 10 draws of at most 9 vertices, at most 5 taps per fragment on the
   factor-1 bodies, one more shader bind and one more texture bind per lens draw than today (5 -> 7 device calls),
   variants built once per program pair. The engine's own depth-stencil is the cheaper alternative (force the
   factor-1 bodies' depth to the far plane and enable `ZENABLE / LESS`), but it gives a hard edge and depends on the
   depth buffer surviving the post-scene views; the RT2 path reuses what the visibility pass already binds. Body 31:
   nothing for this sector (no in-scene emission at the record position); revisit only where a TPlanets sun scene
   is present.

Not verified here: the record -> uv mapping against a picture of the chain, any look of the fade (never engaged
for the sun), cost of the lens bracket in the game.

## 2026-09-22: Run 223 fixes (eligibility, radius, log) and step 2 (per-pixel clip), fixture-verified, not installed, not flown

Worktree on `ab7ecaca`; design: [sun-partial-occlusion.md](../architecture/sun-partial-occlusion.md), "After Run 223". Bottle X3,
CrossOver Preview. No game launch.

What changed: `core::eligible` (the main view's re-probe of a background-view-owned record, `owner+0x270 & 0x400000`; the
main view's own records no longer count in `Ready::records`); `core::footprint` reports a saturated `record+0x34` and the pass
always samples the configured absolute radius (`X3M_SUN_OCCLUSION_RADIUS`, 0.005..0.25, default 0.04 u); `sun_probe` gains
`owner_flags270`, `owner_layer`, `eligible=`; `sun_visibility` gains the owner, `saturated`, `radius_px`, `radius_derived_u` and
the sun lane's projected `lane_u/v`; step 2: `core::classify_body` from the clip rows (CPU), the vertex wrap and the nine-tap
clip pixel wrap (`lens_visibility_variant.cpp`, part 2), the pair cache and the seven-call clipped transaction
(`sun_occlusion_pass.cpp`), RT2 held for the bracket, per-draw `body=` / `centre_u/v` in `sun_lens_draw`, `clipped=` / `other=`
/ `pairs=` in `sun_lens_bracket`; the Present-time back-buffer readback `lens_<device>_<frame>.bgra8` on F8 frames under the log.

| Check | Command | Result |
| --- | --- | --- |
| Host: eligibility (incl. run223's measured pointers and flags), the 648-combination gate restatement, readiness, hold, footprint with the saturated size, body classification, blend, main-view walk, pixel wrap, vertex wrap and its refusals, clip wrap and its refusals, free texcoord, sites, launcher | `/usr/bin/python3 verification/probe/run_host_suite.py --modules test_sun_occlusion` | 20 tests, 0 failing |
| Full host suite | `/usr/bin/python3 verification/probe/run_host_suite.py` | 225 modules, 2236 tests, 0 failing (85 s wall) |
| Hook fixture and GPU fixture | `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_sun_occlusion.py` | passed: hook **74 checks / 0 failures**, GPU **114 checks / 0 failures**; `CALLS execute=33 lens_draw=5`, clipped core draw **7 calls**; record `verification/results/bottle-X3/sun-occlusion.json` |
| Production scratch build | `cmake -S . -B <scratch> -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPython3_EXECUTABLE=/usr/bin/python3 && cmake --build <scratch> -j` | exit 0, 0 warnings |
| x87 audit of that DLL | `python3 verification/probe/check_no_x87.py <scratch>/d3d9.dll` | PASS, 634 reachable functions, 0 violations (the first build failed on `sqrtf` reached from `prepare_lens`: the body classification now uses the SSE square root, `core::sqrt_no_x87`) |

Hook fixture, new among the 74 checks: the owner's own probe (layer 15), a later view's re-probe (layer 100), a foreign record
whose owner has no background flag, and a record the main view owns all reach the original in the same frame as the eligible
probe without disturbing readiness (`still_single`: the latch holds the sun's record, owner and owner flags); the owner losing
the background flag makes the record the original's; the log line carries `eligible=1 owner_flags270=00400001 owner_layer=15`
for the sun and `eligible=0 owner_flags270=00000000 owner_layer=-1 records=1` for the main-view-owned record; the multi-sun
case is now two background-owned records.

GPU fixture, step 2 (320 x 180 sheet at RT2's size, depth edge at column 160, f = 0.5 from the visibility pass, the vs_2_0 with
the lens scene's dp4 shape and identity clip rows, ps_2_0 under ONE/ONE, SRCALPHA/INVSRCALPHA and ONE/INVSRCALPHA):
`lens_prepare` names matrix register 0 and refuses without a vertex program / hash; a core draw is clipped with seven device
calls, the vertex and pixel programs substituted, RT2 on sampler 14 and the fraction on 15: every column left of the edge minus
two equals `f x source` under the law (0.6000, 0.7020, 0.8000 for ONE/ONE), every column from edge plus two equals the
background (0.2, 0.4, 0.6), and the four columns across the edge are the exact ninths 8/9, 7/9, 2/9, 1/9 (measured 0.5098 /
0.2902 / 0.2431 in red for ONE/ONE against 0.6 / 0.2); a ghost draw is the five-call step-1 wrap with uniform `f` over the row;
`Body::Other` and `Body::Unknown` touch nothing (`LensVerdict::Body`); a core draw without RT2 is refused; three repeated core
draws create no program (`pairs=1`, one vertex shader created in the run); `before_reset` releases 11 objects (the clip pair's two
blocks among them); after the native Reset a core draw clips again with no program created. Every transaction is bracketed by
the byte comparison of the device state (now with hostile state on sampler 14 too).

Not verified: anything in flight (whether the eligible probe is taken for the sun, the uv mapping against the picture, the
radius default against the visible glow, the body classification against the real bodies, the look of the clip edge), native
Windows execution, cost in the game (the counts are: one validated engine_memory read per eligible-shaped probe, per core lens
draw seven device calls and nine RT2 taps per fragment).

### Review fixes (same day, merged main `6019d936`)

Deep review of `e0d7abb6`: no blocking defect; five fixes. (1) The clip's uv gains half a texel (`cC.zw = 0.5 + dx/2,
0.5 + dy/2`): a fragment's clip-derived uv is a texel edge under D3D9's pixel-centre rule, so the taps now sit on texel
centres on any implementation. (2) A core body is clipped only (`out *= open_px`); ghosts and streaks carry `f`;
`X3M_SUN_OCCLUSION_CORE_F=1` / `--sun-occlusion-core-f` restores the product for the flight comparison (design note,
"Step 2 as built", for the argument). (3) Classification precedes any build: `lens_prepare` scans without creating a
shader, the vertex wrap is created on the first core draw, other records' bodies and log-only mode never build one, and
an unclassifiable body (non-conforming vertex program, unknown origin, rows in no shadowed window) is `Other`
(untouched, logged once per pair as `sun_lens_body_unclassifiable`) instead of a process-wide block. (4) The vertex scan
validates the position source is `(position.xyz, 1)` (`origin_known`); any other shape classifies as `Other`. (5) The
flight checklist names the second-`0x400000`-view risk (`multi_record_frames`, `sun_visibility single=0`).

| Check | Result |
| --- | --- |
| merge of main (`6019d936`, includes `3eccbaf6`) | clean |
| `/usr/bin/python3 verification/probe/run_host_suite.py --modules test_sun_occlusion` | 21 tests, 0 failing (new: origin validation over ten vertex shapes, the half-texel constant, `core_f` on / off, the `--sun-occlusion-core-f` option) |
| `/usr/bin/python3 verification/probe/run_host_suite.py` | 225 modules, 2237 tests, 0 failing (87 s wall) |
| `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_sun_occlusion.py` | passed: hook 74 / 0, GPU **122 / 0**; core clipped only: open half = source (1.0000 / 1.0000 / 1.0000 for ONE/ONE at f = 0.5), covered half = background, edge ninths 8/9, 7/9, 2/9, 1/9 with the half-texel offset; `core_f`: open half 0.6000 / 0.7020 / 0.8000 (= f x source + bg); `lens_prepare` scans without creating a shader (0 programs created, `first` once); a vertex program with `mul r0, r0, c4` after the mov scans with `origin_known = 0` and a core draw through it is refused with nothing created; 7 calls per clipped draw; Reset then clip again with no program created |
| scratch build + `check_no_x87.py` | exit 0, 0 warnings; PASS, 636 reachable functions, 0 violations |

