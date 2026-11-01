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


## 2026-09-22: Run 228/229 triage (Run64 e839dc7c): shimmer is a 1/32-tap quantization step, lens dumps are a copy-step gap, CORE_F never engaged

Flights `/tmp/x3-bottleX3-run228` (`--sun-occlusion --sun-occlusion-log`, `X3M_VOLUMETRIC_FOG=0`) and `/tmp/x3-bottleX3-run229`
(same plus intended `X3M_SUN_OCCLUSION_CORE_F=1`). Diagnosis only; nothing changed.

**A. Five F8 bursts (`present_1_*` frame ranges), `sun_visibility` over each (all `single=1 answered=1 ran=1 saturated=1
radius_px=51.2` except burst 5):**

| Burst | Frames | f_used | Matches user's order |
| --- | --- | --- | --- |
| 1 | 3440-3471 | 1.0 constant | sun fully visible |
| 2 | 5511-5542 | 0.4666-0.4985 (mean 0.475) | partially occluded by station |
| 3 | 6635-6666 | 0.0 constant | fully occluded by station |
| 4 | 10520-10551 | 0.0678 constant (f_raw 0.0938) | fully occluded by a stationary ship (small residual leak, not exactly 0) |
| 5 | 14646-14677 | `single=0 answered=0` (no eligible record this burst) | flying toward the station, SETA cancelled |

**B. Shimmer (burst 2).** `f_used` std = 0.0136 over 32 frames; `f_raw` alternates between exactly 0.4688 and 0.5000 (delta
0.03125 = 1/32) with no `held`/hold path engaged (`skip=none ran=1` every frame): this is a single 32-tap disc sample
flipping open/closed frame to frame, not the 4-frame hold. Record position (`x=-4812 y=13209`) is constant; the flip
correlates with the sub-pixel camera jitter each frame (`X3M_MOTION_JITTER=1`, TAA on), which moves the projected disc
by a fraction of an RT2 texel across the station edge. Not measured further: a frame-by-frame back-buffer disc-edge
pixel walk (no `lens_*.bgra8` exists for this burst's frames, see C) and the exact jitter offset per frame (present but
not cross-referenced here for time).

**C. Missing `lens_*.bgra8` files: not a source defect.** `sun_lens_readback` logged 128 lines in run228, all
`result=00000000` with the expected byte count (1280x768x4 = 3932160). The files exist, on disk, at
`~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/x3-modern-captures/lens_1_<frame>.bgra8` (128 files,
frame numbers matching all 5 bursts) — the same directory that also holds every `color_/present_/depth_/...` file of
this and many other sessions (34,526 files total, accumulated since 2026-09-10). `/tmp/x3-bottleX3-run228` is a subset
of that directory; whatever produced it copied every other prefix family for these frame ranges but not `lens_`. Source:
`MotionOutput::sun_lens_present_readback` (`src/proxy/motion_output_sun_occlusion_inc.h:140-149`), called from
`before_present()` unconditionally after `readback()` — code and gating (`capture_`, `lens_chain_drawn_`) are sound and
did run.

**D. Fifth burst (fast approach).** `X3M_VOLUMETRIC_FOG=0` in this run (from the `proxy_options` line): fog is ruled out
as a cause. `X3M_TAA_SENTINEL=auto`, `X3M_TAA_SENTINEL_STABILISER=0.7`, `X3M_TAA_UNMATCHED_STATIC=node` were active.
One sample frame (14660): 88,747 pixels with `color_` luminance > 120, of which 1,409 are more than 40 luminance units
darker in `present_` than in `color_`, spread over most of the frame (bbox nearly the full 1280x768) — this crude
global count cannot localize a smear trailing the station without first identifying the station's screen region, which
was not done here. Candidate sources, not decided: TAA history reprojection of station pixels, the sentinel stabiliser,
the sun pass (inactive this burst, `single=0`). **Open**: needs a targeted ROI comparison of `color_`/`taa_`/`present_`
around the station silhouette across consecutive frames of burst 5, plus the TAA sentinel/unmatched-static log lines for
those specific frames (not extracted here).

**E. CORE_F never engaged in run229.** `sun_occlusion_config override=1 log=1 route=1 radius_u=0.0400 curve=1.000
core_f=0` is the only `core_f=` line in the 70 MB log — the config banner is emitted once at startup and shows
`X3M_SUN_OCCLUSION_CORE_F=1` did not take effect for this run. 19,745 `sun_lens_draw body=core clipped=1` lines exist
and 1,513 frames have `f_raw` strictly between 0.05 and 0.95 (partial occlusion did occur), so the run was capable of
showing a CORE_F difference — it simply never ran with the option on. "No visible difference" is explained by this, not
by an absence of partial-occlusion frames.

Not verified: the shimmer's exact jitter-frame correlation (offsets not pulled); the smear's spatial correlation with
the station and its motion vector; whether `/tmp/x3-bottleX3-run228`'s omission of `lens_*.bgra8` is a fixed pattern of
the copy tool or a one-off (the copy tool that produced the run directories was not located in this repository).

### Follow-up (same day): burst-2 edge walk and burst-5 dark-pixel geometry, `lens_*.bgra8` now present

`lens_1_<frame>.bgra8` files are now in `/tmp/x3-bottleX3-run228` (snapshot tool fixed by another agent to copy
`sun_lens_readback`). Re-did the two open items with them.

**1. Burst 2 (5511-5542) edge walk.** Sun disc centre from `sun_visibility` (`u=0.42657 v=0.29845`, constant all 32
frames) -> pixel (546, 229). Scanning `lens_1_<frame>.bgra8` (the back buffer **after** the lens draws) along y=229,
the disc's saturated (luma=255) plateau ends at a **binary** edge position: x=535 in 26 of 32 frames and **x=537 in
exactly 6 frames** (5515, 5517, 5523, 5525, 5531/5533, 5539/5541 — every occurrence of `jitter_index` 4 or 6, no
others; 32/32 match). `jitter_x`/`jitter_y` per index (from `motion_output_frame`): idx4 `x=+0.125 y=+0.2778`, idx6
`x=+0.375 y=+0.0556`; the other six indices (0,1,2,3,5,7), including one with a *larger* positive `jitter_y`
(idx7, `y=+0.3889`), all give edge=535 — so the flip is not a simple linear function of `jitter_x` or `jitter_y`
alone, but it is a **deterministic function of `jitter_index`** (perfectly reproduced across all four 8-frame
sub-cycles of the burst). `depth_1_<frame>.rgba32f` (`.r`, the raw per-frame device depth, sentinel -1) at the same
row shows the silhouette edge itself also at x≈536-538 and also varies by jitter_index (536.5 at idx0/1, 537.5 at
idx4/6 in the frames sampled) — i.e. **both** the raw depth and the lens clip move with jitter, but `present_1_`
(captured *before* the lens draws, per `before_present()`'s call order) has no saturated disc pixel in this row at
all (it precedes compositing), so it cannot serve as an "unjittered reference" as hoped; there is no dump of the
resolved/TAA output before the sun draws distinct from one after. **What is shown numerically:** the visible clip
edge takes only two discrete positions synchronized 1:1 with `jitter_index` (not a smooth per-frame drift), which is
consistent with the disc edge sampling a jittered RT2 at a texel granularity coarser than one screen pixel (RT2 is
320x180, a 4x/4.27x downsample) — a small sub-pixel jitter occasionally flips which RT2 texel is nearest the query
point, producing a 2-screen-pixel pop rather than continuous sub-pixel motion. **Not settled:** whether the
*resolved* station silhouette (independent of the sun draws) is itself stable across the same 8 phases, because no
capture in this flight isolates that (would need a readback between TAA resolve and the lens bracket).

**2. Burst 5 (14646-14677) dark-pixel geometry.** Station bbox via 4-connected flood fill of `depth_1_ .r > -0.5`
from the frame centre: grows from (651-707, 296-379; area 1892 px) at frame 14646 to (651-715, 287-376; area 2386 px)
at frame 14677 (bbox centre drifts from (679,338) to (683,332), a small up-right creep while mostly growing in
place — consistent with slowly closing in, not a fast lateral pan). Dilating by 24 px and comparing `color_1_`
(pre-TAA) vs `present_1_` inside that window: pixels more than 40 luma darker in `present_` than `color_` grow from
333 to 536 across the burst (ratio to silhouette area rises from 0.176 to 0.223, i.e. slightly faster than the
silhouette itself). Their centroid sits at a **consistent offset from the bbox centre**: `offx` mean +1.27 px
(std 1.37, 25/32 frames positive), `offy` mean **-3.47 px** (std 1.66, **32/32 frames negative**) — i.e. the dark
pixels sit persistently just *above* the growing silhouette's centre, not trailing behind its direction of growth
(which is itself nearly symmetric, not a strong lateral sweep) — this does not match a classic motion-trail ghost
smeared opposite the direction of travel; it looks more like a fixed-direction rim/shadow band that scales with the
silhouette. `camera_cut=0` and `camera_rotation_deg=0.2166` (constant, small, steady) for all 32 frames — no cut, no
big rotation. No `motion_unmatched_static` or `taa_invalidate` line fired in this frame range (grepped, zero hits):
the TAA sentinel/unmatched-static machinery did not flag anything here, which rules out a *logged* invalidation
event as the cause and leaves ordinary TAA history reprojection of the growing silhouette's edge, or the sentinel
stabiliser's un-logged per-frame blend, as the remaining candidates; `single=0` throughout (from part A), so the sun
pass itself is inactive and not a candidate for this burst.

Not verified: whether the dark band is specifically the silhouette's *own* previous-frame position (an explicit
per-pixel earlier-position overlay was not constructed, only the centroid-offset statistic above); the sentinel
stabiliser's internal per-pixel decision (not logged per frame at that granularity).

### Follow-up 2 (same day): burst-5 dark pixels vs sentinel-stabiliser-radius hypothesis

Chebyshev distance of every dark pixel (`present_1_` >40 luma darker than `color_1_`, dilate-30 ROI) to the nearest
`depth_1_ .r > -0.5` (station) pixel, summed over all 32 frames (13,766 dark pixels): dist0=6458, dist1=6902,
dist2=377, dist3=27, dist4-6=0, dist7+=2. **99.4 % sit at distance 0-1**, i.e. on the silhouette itself or its
immediate 1-px ring, not spread out to a 3-px (7x7) radius — inconsistent with a radius-3 box pulling dark history
3 px into the sky. 53.1 % of dark pixels are themselves sky (sentinel-depth) pixels, essentially all at distance 1
(the ring immediately outside the silhouette). Mean luma deficit falls with distance: 77.6 at dist0, 64.2 at dist1,
47.9 at dist2, 43.0 at dist3 (n=2 at dist7+, not meaningful). Dark-pixel count / silhouette perimeter per frame:
0.644-0.912 (mean 0.777) — roughly three-quarters of a perimeter's worth of dark pixels per frame, consistent with a
thin (~1 px) band, not a thick 7x7 smear. **Control (>40 brighter in present than color):** 6,750 pixels, **100 % at
distance 0** (on the silhouette itself) and **0 % sky pixels** — the brightening is confined to the silhouette's own
pixels, never the surrounding sky, so this is not a symmetric resolve difference: darkening leaks outward by about
1 px into the sky half the time, brightening never does. Net: the evidence fits a 1-pixel-wide reprojection/AA edge
effect (or a 2x2-tap resolve filter) better than the described 7x7/radius-3 sentinel box; a radius-3 box is not
supported by this distance histogram.

## 2026-09-22: Jitter-stable clip and visibility (worktree, not installed, not flown)

Cause confirmed in source: RT2 is rendered on the jittered raster (`apply_jitter`, +jx px right / +jy px down)
and the lens bracket runs after `hook_scene_end`, where `scene_bound()` refuses, so the lens draws, the record's
`u/v` and `lane_u/v` are unjittered. Two changes. (1) The visibility pass reads every tap at `p + (jx / W, jy / H)`
(new `c2.xy` of `sun_visibility_ps.hlsl`, recompiled: 1675 words, bytecode sha256 `bd5a82ae…7472`;
`core::jitter_uv`, zero when the jitter is off; the pass validates ±0.05 uv and records / restores c0..c2). (2) The
clip's taps are texel-centred point taps (`floor(i + 0.5 + j) = i` for |j| < 0.5: an offset would be a no-op), and
the clip's edge column is the jittered raster's silhouette column, which moves one texel between phases; so the
kernel now bounds that step instead: the ±1/±2 cross (ninths; a one-texel shift moved a column by up to 5/9) is
replaced by the fragment plus the eight knight moves (±2, ±1) / (±1, ±2), one ninth each (`cK.w = 1/9`,
`cD = 2dx, dy, dx, 2dy`, `cW = −2dx, dy, −dx, 2dy`: every tap one signed swizzle from the fragment's uv, no tap
depending on the one before; the half-texel rule kept). Every column, row and 45° diagonal of taps weighs at most
2/9 = 0.222 (an exhaustive search of symmetric integer-offset kernels of ≤ 17 taps on a 1/64 grid finds no bound
below 7/32, and the knight set is the only 9-tap shape reaching it), the profile across an edge is 1, 7/9, 5/9, 4/9,
2/9, 0 over five columns. Slots: 40 arithmetic / 11 texture for the lens program, unchanged, one more `def` constant
(four free constants needed, `resource_limit` otherwise); the pair cache, the 7 device calls per core draw and the
pass's own program (1675 words, under 512 slots) are unchanged. Limitation: the c2 offset assumes every RT2 depth
writer was jittered (`unjittered_depth_writers` in `motion_output_frame`; architecture note, "Jitter contract"). The `sun_visibility` line gains `jitter jitter_index jitter_x
jitter_y`.

| Check | Result |
| --- | --- |
| `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_sun_occlusion.py` | passed: hook 74 / 0, GPU **128 / 0**. New: an occluder rasterized into an R32F RT2 through the lens vertex program with `jitter_rows` under the temporal pass's eight offsets (`JITTER_PHASES` matches `run_motion_output.expected_jitter`). Vertical edge at texel x = 164.703: corrected `f_raw` = 0.56250 (18/32) in all 8 phases, bit-identical to the unjittered raster's (also `f_smoothed`, `f_used`); uncorrected (pre-fix read) = 0.53125 in phases 1, 3, 7 (jx = −0.25, −0.375, −0.4375), 0.56250 otherwise, exactly the CPU model. Horizontal edge at texel y = 82.153: corrected 0.37500 (12/32) in all 8, uncorrected 0.40625 in phase 7 (jy = +0.389): the y sign. Clip against the same jittered RT2: every phase's row equals the kernel model of that phase's silhouette column (`ceil(e − 0.5 + jx)`: 165 in phases 0, 2, 4, 5, 6; 164 in 1, 3, 7), monotone over columns b−4 .. b+3; per column, the largest \|Δ open\| between any two phases: **measured 0.2255** (8-bit readback of ONE/ONE), kernel model 0.2222, former cross model 0.5556; 5 columns move (each by ≤ 0.2255). Existing clip checks re-baselined to the kernel's ninths (edge columns 7/9, 5/9, 4/9, 2/9 of the source; `core_f` the same times f); 7 calls per core draw, Reset re-clips with no program created |
| `/usr/bin/python3 verification/probe/run_host_suite.py --modules test_sun_occlusion` | 22 tests, 0 failing (new: `jitter_uv` sign / off / zero-size, CTAB of the compiled program `disc c0, control c1, jitter c2`, fixture phases = temporal pass sequence, the exact clip wrap words with the knight offsets and weights, the kernel's axis / diagonal bound and edge profile, four-constant refusal, check count 128) |
| `/usr/bin/python3 verification/probe/run_host_suite.py` | 226 modules, 2244 tests, 0 failing (168.0 s wall, concurrent with the build) |
| scratch build `build-sunjit` (MinGW i686 toolchain, RelWithDebInfo) + `check_no_x87.py` | exit 0, 0 warnings; PASS, 634 reachable functions, 0 violations; `d3d9.dll` sha256 `65da72a5…` (worktree build, not a candidate) |

Second-review fixes (same day, in the numbers above): (1) the architecture note carries one soft-edge profile. (2)
`scan_pixel` counts ps_2_0 instruction slots (lrp 2, crs 2, m3x2 2, m3x3 / m4x3 / nrm / pow 3, m3x4 / m4x4 4,
sincos 8, dsx / dsy 2, if / ifc / loop / rep / breakc 3, endloop / endrep 2; texld / texkill 1, texldd 3) and refuses
`resource_limit` when the original plus the wrap (clip 32 arithmetic + 10 texture, step 1 3 + 1, both counted from
the emitted words) would exceed 64 / 32: wined3d does not enforce the limits, native D3D9 does. ps_2_x is held to
the same floor. Host test at the boundary: 31 extra `mov` applied / 32 refused; 28 `mov` + `pow` (3) applied / 29
refused; 21 extra `texld` applied / 22 refused; step 1: 60 / 61 `mov`, 30 / 31 `texld`. (3) Native parity of the
wraps' `def` registers: no existing path restored constants (the blocks captured shaders, textures and sampler
states only). The recorded blocks are now keyed by (fraction sampler, depth sampler, the wrap's `def` registers)
and record `SetPixelShaderConstantF` for each of those registers, so the per-draw `Capture` reads the
application's values and the `Apply` after the draw puts them back: 5 calls per ghost draw and 7 per clipped draw
as before (the register saves ride in the existing capture / apply; one block pair per distinct layout, capacity
8). Fixture: the state snapshot now compares c0..c31 with hostile garbage on c28..c31 (the clip wrap's registers)
and every `_state_restored` check passes; `before_reset` releases 13 objects (the ps_2_0 and ps_3_0 ghost wraps
have different `def` registers and therefore separate blocks). Under wined3d a `def` never touches the constant
file, so the readback cannot show the native failure; the fixture proves the block records and restores the
registers.

## Run 235 sun check (2026-09-22)

`sun_occlusion_config` shows `core_f=1` (one banner line, as before). Burst 2
(6967-6998) has the sun out of view the whole time (`sun_visibility skip=record
... f_raw=-1.0000` every frame) so it gives no edge data. Across the rest of
the run's `single=1 answered=1` samples, the still half-covered case
(`f_raw=0.6562`) recurs at every `jitter_index` 0-7 with no alternation by
jitter (counts 97-143 per index, same value) - the shimmer fix holds; matches
the user's "shimmer reduced or gone" report. Not independently re-derived
beyond this count; no new edge-walk measurement was taken this run.

## 2026-09-22: core dimming is the default (run235 acceptance)

Run 65 session A, run235: with the core-`f` A/B flown, the user accepted the product form, so
`rO *= f * open_px` for the sun's clipped core bodies is now the default whenever `--sun-occlusion` is on.
`--sun-occlusion-core-f` became an explicit opt-out (`on` / `off`, default `on`); `off` restores the Run64
clip-only form. The launcher sets `X3M_SUN_OCCLUSION_CORE_F` to `1` or `0` only when the override is requested
and pops any inherited value otherwise (nothing is patched without `--sun-occlusion`); the DLL reads the variable
as default-on, only an explicit `"0"` turning it off, and the `sun_occlusion_config ... core_f=` field keeps its
meaning (1 = product, 0 = clip only). No GPU or shader change: both wraps already existed and both fixture cases
already passed, so no Wine run was needed.

| Check | Result |
| --- | --- |
| `/usr/bin/python3 verification/probe/run_host_suite.py --modules test_sun_occlusion` | 24 tests, 0 failing (new: core-f default on / explicit off / inherited value ignored, the DLL's default-on read; the invalid-choice refusal) |

## Launcher default (2026-09-25, after Run 83)

User decision after the Run 83 flight: "--sun-occlusion works, make it the default along with the sun dimming when
occluded". Run 83 A (run323, launches 1 and 3) had shown the vanilla CPU probe hiding the whole flare at about half
cover: `verification/results/run323-run83a-launch1-3/sun_occluder_cover.py` (geometry fraction of `depth_1_<frame>` in
boxes of half-size 5/20/60 px around the sun pixel, output in `sun_occluder_cover_out.txt`, measured) gives, while the
flare shows, frames 28222/28229 h5=0.000 h20=0.189-0.215 h60=0.292-0.299, and once it is gone, frames 29032/29039
h5=1.000 h20=0.989-0.992 h60=0.538-0.540: the sun pixel is covered with the disc neighbourhood about half covered.

Launcher (`tools/manage.py`, the c2aa8d4e pattern): on a modded launch with `--motion-output` and without
`--submit-phases`, `--no-sun-occlusion` or `--sun-occlusion`, the launcher sends `X3M_SUN_OCCLUSION=1` with
`X3M_SUN_OCCLUSION_DEFAULT=1` (and `X3M_SUN_OCCLUSION_CORE_F=1`: the core dimming of run235 is the "sun dimming when
occluded" and comes with it); explicit `--sun-occlusion` sends marker 0. `--no-sun-occlusion` sends nothing and drops
inherited values; the DLL needs the absence (it installs only on `X3M_SUN_OCCLUSION` exactly `1`), so absent still
means nothing patched, no cost. With `--submit-phases` the default is simply not sent (explicit `--sun-occlusion`
stays refused there); without `--motion-output` and under `--vanilla` nothing is sent. `--sun-occlusion-radius`,
`-curve` and `-core-f` accept the default as their prerequisite. DLL: `sun_occlusion_config` gains `default=` (1 only
when the override is on and the marker is `1`; the marker is not read otherwise); the DLL default when the variable
is unset is unchanged (off), so the fixtures are unchanged and no Wine run was needed. `run_motion_output.py` and
`run_lod_occlusion_patch.py` drop an inherited `X3M_SUN_OCCLUSION_DEFAULT`.

| Check | Result |
| --- | --- |
| `cmake --build build -j4` (MinGW i686, RelWithDebInfo) | 0 warnings |
| `python3 verification/probe/check_no_x87.py build/d3d9.dll` | 690 reachable functions, 0 violations |
| `python3 -m unittest test_sun_occlusion test_taa_box_resolution` | 36 tests OK (test_sun_occlusion 27: default 1/1, explicit 1/0, opt-out, no route, `--submit-phases`, `--vanilla` nothing; dependent options under the default; the row's `default=`) |
| 51 launcher modules (every `verification/analysis/test_*.py` naming `manage.py`) plus test_taa_box_resolution | 726 tests OK |
| Dry runs (Run 83 A stand command via `tools/manage.py launch --bottle X3 --dry-run`; `verification/results/sun-occlusion-default/dry_runs.sh`, output `dry_runs_out.txt`) | stand: `X3M_SUN_OCCLUSION=1`, `_CORE_F=1`, `_DEFAULT=1`; `+ --sun-occlusion`: `_DEFAULT=0`; `+ --no-sun-occlusion`: none; `+ --submit-phases`: none, exit 0; `--vanilla`: none |

## 2026-09-29: lens-flare gain (worktree, not installed, not flown)

`lens_flare_gain` / `--lens-flare-gain G` (0..1, default 1 = off): every ONE/ONE/ADD lens-bracket draw goes out with
`SRCBLEND = BLENDFACTOR`, `BLENDFACTOR = G` (ZERO at 0), restored after the draw; the sun's bodies follow it (f x G
over the wrap). Design and refusals: `docs/architecture/sun-partial-occlusion.md`, "Lens-flare gain".

| Command | Result (measured) |
|---|---|
| `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_sun_occlusion.py` (record `verification/results/bottle-X3/sun-occlusion.json`) | passed; hook 74/0; GPU 135 checks, 0 failed, 0 skipped (128 + 7 lens-gain); 0.8,0.6,0.4 quad over black: G = 1 reads 0.800,0.600,0.400; G = 0.5 (applied 128/255) 0.400,0.302,0.200; G = 0 0,0,0; G = 0.5 over the ghost wrap at f = 0.5 0.200,0.149,0.102 (expected 0.201,0.151,0.100); every case restores the hostile application BLENDFACTOR and the snapshotted states; wined3d (the bottle's builtin D3D9) |
| same run, `LENS_GAIN_COST` | 113 draws x 4 SetRenderState calls (the transaction without the draw) = 4.81 us per frame on wined3d |
| `cmake --build build` (MinGW i686) / `check_no_x87.py build/d3d9.dll` | 0 warnings / 722 reachable functions, 0 violations |
| `tools/config/generate.py --check` | PASS, 241 settings, 93 in the template |
| host modules test_launcher_defaults (9, one new), test_config_schema (17), test_sun_occlusion (27, expected GPU checks 135) and the other launcher-parsing modules | all OK |

Not covered by a fixture: the `MotionOutput` integration (`prepare_lens` -> `after_draw` through the draw hooks); the
lens bracket opens only from the engine thunk. A first runner attempt at 05:25 hung in the GPU executable (1 s CPU in
15 min, killed by the runner's 900 s timeout); the same binary then passed directly in 4.5 s and through the runner in
8.2 s (cause not identified).

Review fixes (same day, no Wine rerun): a `lens_flare_gain_frame` row every 300 frames while G < 1 in every tier
(draws, gained, refused per reason); launcher help names both redirects; the runner feeds the GPU executable
`stdin=DEVNULL` and on a timeout writes the partial output into `sun-occlusion-gpu.txt` and the record before raising.
The committed record's source hash of `run_sun_occlusion.py` predates that runner edit. Build 0 warnings, x87 0
violations (722 functions), host modules test_launcher_defaults 9 / test_config_schema 17 / test_sun_occlusion 27 OK.

**Default 2026-09-29 (user decision after Run 103 A):** Run 103 A flew `lens_flare_gain 0.5` with `emission_source_clamp 0.7` and was accepted ("working now, no issues"; run365 `object_lifetime_stats peak_live=44913` past the old 16,384 confirmed the capacity cause, figure from the run triage). The user chose `lens_flare_gain = 0.3` as the default: schema default `0.3` (bare DLL and template) and the launcher fills `--lens-flare-gain 0.3` on every modded `--motion-output` launch, sent as `0.3` (fixed-point, trailing zeros dropped; explicit values too, e.g. `0.5`, no longer `0.5000`). The lens bracket's two redirects therefore install on every modded launch, observe-only under `--no-sun-occlusion`; `--lens-flare-gain 1` is the opt-out. The `lens_flare_gain_frame` row now appears in every default flight. Measured: host modules test_launcher_defaults 9, test_config_schema 17, test_sun_occlusion 27 OK; build 0 warnings, x87 0 violations; no Wine run (the GPU fixture compiles `lens_flare_gain.h` with explicit gains; fixtures run `X3M_CONFIG=bare`).

## 2026-09-29: lens-flare gain 0 skips the draw (worktree, not installed, not flown)

At `lens_flare_gain 0` an admitted lens draw (bracket open, route on, not recording, plain, state known, ONE / ONE /
ADD) is no longer submitted with SRCBLEND ZERO: the hook returns S_OK without the native call and without a setter
(`Law::skip`, `apply_lens_gain`); `lens_flare_gain_frame` gains `skipped=`. G > 0 paths unchanged. Purpose: one flight
compares frame time at 0 against 0.3 to price the lens chain's ~170 submits per frame (run375). Design:
`docs/architecture/sun-partial-occlusion.md`, "Lens-flare gain". `X3M_FIXTURE_BOTTLE=X3 python3
verification/probe/wine_lock.py python3 verification/probe/run_sun_occlusion.py` PASS (measured): hook 74/0, GPU
137 checks / 0 failed (two new: `lens_gain_occlusion_query`, `lens_gain_zero_not_issued`). Over a 26,51,77 background
the zero case reads back 0.102,0.200,0.302 with 0 issued draws and 0 occlusion-query samples; off / half /
half_wrapped issue one draw each with 256 samples. The fixture mirrors the production decision on the header's law;
the `MotionOutput` integration through the draw hooks still has no fixture. Build 0 warnings, x87 0 violations (726
reachable functions), `generate.py --check` PASS, host modules test_sun_occlusion / test_launcher_defaults /
test_config_schema / test_logging_tiers 63 tests OK.

## 2026-09-29: lens-flare gain 0 culls in the engine (worktree, not installed, not flown)

At `lens_flare_gain 0` the DLL now also claims the engine-side cull `lens_flare_cull` (`src/proxy/lens_flare_cull.{h,cpp}`,
`lens_flare_cull_core.h`; no new key; values above 0 unchanged): a second stub on the small-parts claim of the cull/LOD
pass 0x0047cfe0 at 0x0047d2a2 sends a node whose model id (+0x140) is one of the 42 flare bodies down the engine's own
size-cull instruction 0x0047d2c3, so the lens block's cull walk (0x00472471 -> 0x0047e780 -> 0x0047cfe0 per root)
leaves the sprites unrenderable and the traversal 0x0047e6e0 issues no draw. Purpose: measure whether the engine's
24-26 us per issued draw (run384/run385 `views`) is recovered when the ~115-181 flare draws per frame never reach the
engine's per-draw work, against Run 105 A's ~1 ms from the proxy-side skip. The proxy skip (`Law::skip`) stays as it
is: the fallback when the claim is refused, otherwise idle (`lens_flare_gain_frame` keeps reporting; `skipped=` should
read 0). Review (same day, no blocker): four fixes applied before install, listed in the RE note's "Verified" paragraph;
the fixture, the three host modules and the DLL build were rerun afterwards (rows below are the rerun's numbers). Site, stub, body set and rows: [lod-selection.md](../reverse-engineering/lod-selection.md), "Lens-flare cull
on the small-parts site". The flare sprites do pass through 0x0047cfe0 (the pass's only callers are its recursion and
the root walker the lens block calls; the run385 census rows carry every drawn `v\` sprite), so no other site was claimed.

| Command | Result (measured) |
|---|---|
| `python3 verification/results/lens-flare-cull/pass_callers.py callers 0047cfe0` (capstone, installed EXE; output `pass_callers_out.txt`) | callers of 0x0047cfe0: 0x0047d53c (recursion), 0x0047e7a5 (the walker); callers of 0x0047e780: 0x0047226b, 0x00472471 (lens block), 0x0047e8c2 |
| `cd verification/probe && python3 verify_lens_flare_cull_site.py` | PASS 13/13 (small-parts window re-verified 20/20; pass callers exactly the two; lens walk, walker call, model read bytes; 42 distinct default-form names) |
| `python3 verification/probe/build_cull_small_parts.py` then `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_cull_small_parts.py` (record `verification/results/cull-small-parts-cpu.json`) | 200 checks / 0 failures in 5.8 s; lens section 58 / 0: default-form and literal names, case fold, bitmap span, stub layout; a synthetic body table resolves 37 names by id and 3 by scan (40 mapped, 2 absent), grows (+1, new slot only), moves (restart, `v\01006` re-bound 20000 -> 20002), shrinks (37, still armed), is refilled in place at the same address and count (the mapped dynamic slots re-read per frame catch it: restart, 20000 -> 20002 within one frame, no restart while stable), unreadable (set stands); `LENS BENCH` restart over 13,200 slots (2,200 named dynamic) 210.7 us, per-frame re-read of 5 mappings 0.134 us (Wine/FEX, harness-inclusive); G > 0: flag 0, every node and EAX/ECX/EDX/EFLAGS native; G = 0: the fixed and the dynamic flare body end byte-identical to the engine's own size cull (except the limit word), plain / model-less / small nodes untouched, count 2, LastError and callee-saved registers preserved, the 300-frame row; both stubs chained in both orders (small node by the small-parts stub, flares by this one); the owner's shutdown restores the site with both chained, the flag drops; closed window refused |
| `PYTHONPATH=verification/probe:verification/analysis python3 -m unittest test_lens_flare_cull test_cull_small_parts test_logging_tiers test_launcher_defaults test_config_schema test_sun_occlusion test_dust_leak_fix_site` | 98 tests OK before the review fixes; after them `test_lens_flare_cull test_cull_small_parts test_logging_tiers` 38 OK (test_lens_flare_cull: core compiled on the host and its stub bytes equal to the verifier's twin, 39/42 names over a synthetic table, synthetic image passes all but identity and each pinned span changed is refused, wiring and docs pinned, installed EXE PASS) |
| `cmake --build build -j8` (MinGW i686, worktree) / `python3 verification/probe/check_no_x87.py build/d3d9.dll` | 0 warnings / PASS, 734 reachable functions, 0 violations (the stub is emitted bytes; begin_frame/report are plain integer reads) |
| `python3 tools/config/generate.py --check` | PASS (no schema change) |

Not covered: the flight (the count row against `frame_timing` / `frame_phases views`), and a mod whose Lensflares
table names bodies outside the 42 (they would draw; `lens_flare_gain_frame skipped=` then shows them).

### Lens-flare engine cull: flight Run 110 A (run387/run389, 2026-09-29)

Measured ([results](../../verification/results/run386-389-cull-ab/)): `lens_flare_cull status=patched`, 42 of 42 names
resolved, 0 restarts; 150-176 flare nodes culled per frame (166.3 right after load against 166.2 flare draws per frame in
the runs without the cull). Frame time at the carrier view without `--perf`: gain 0 (run387) 17.27 / 17.41 / 18.78 ms
against defaults (run386) 17.43 / 17.56 / 18.10 ms, a difference of 0 ± 0.5 ms (noise). Under `--perf` (run389) the
overlays phase, where the flares are drawn, falls from 2.68 ms (run385, 168 flares) to 0.20-0.25 ms and the frame by about
1.2 ms, so the engine's per-flare work does vanish with the node, but it is not measurable in a plain run. `skipped=`
stays at about 0.2 per frame: `v\01009` (dynamic id) and `v\10667` are outside the 42-name set. Verdict: the cull stays
available at gain 0; no flare batcher (the recoverable cost is under the noise without instrumentation).

## 2026-11-01: Run216 sun through hulls (AP native, 1.8.2): an unclassifiable sun body hands the record to the vanilla probe (worktree, not installed, not flown)

Triage (`verification/results/run216-sun-occlusion/`, from the user's log with `sun_occlusion_log = 1`, measured): the sun's
lens chain on this install is one vs_3_0 / ps_3_0 draw per frame, `vs 494fe349b8bc12ec / ps 7c83ed50c9894e44` (one quad,
2048² texture, ONE/ONE), drawn in 4,112 of the 4,113 sun frames; the run223 vs_2_0 pair `d5e1c753 / 8360f422` belongs to
ship and station lights here (groups 36 / 37 / 4). The lens scan refuses every SM3 pair (`no_free_texcoord`), so the
body was `Other` and drawn untouched with Z off while the override answered "visible" on 4,111 of 4,113 frames: the
engine's own all-or-nothing hiding was defeated. The pass itself works (raw fraction 0.06-0.41 while occluded).
Offline inventory (`lens_pair_inventory.py`, output `lens_pair_inventory.json`, measured): both programs are **stock AP**
content, in `addon/01.cat`, `ps_7c83ed50` only in the four `shader/3_0/**/standard_lighting.fb` effects; `/mnt/c/X3`
(Mayhem as numbered `addon/05..15`) resolves to the same 2,352 effects and 751 programs as `/mnt/c/X3AP-orig`; FL
(`/mnt/c/X3FL`, root + addon2) has the vertex program but not `ps_7c83ed50`. So the shaders do not come from Mayhem;
Run 223 (2026-09-22) predates the Mayhem install (2026-09-28): it was stock AP under CrossOver, whose sun (group 5,
1280x768) drew 10 draws per frame of the vs_2_0 pair (section C above). Whether stock AP on native Windows draws its
sun with the SM3 pair (the model's material choice) is unknown.

Fix (`core::Deferral`, `sun_occlusion.cpp`, `prepare_lens`): a lens body the scan cannot classify (`body_other`: a refused
SM3 pair, an unknown origin, rows in no window, a full scan cache) in the bracket of the frame's single eligible record
defers that record: the override stops answering for it (`Ready::probe` gets `enabled = false`), so the vanilla probe
hides the chain behind geometry from the next frame on. In an answered frame the draw takes the refusal path (it and the
rest of the bracket are dropped, `lens_suppress_`), never drawn unscaled. Unlike `block()` it is per record and lifts:
the bodies are classified in every frame with a single record while the override is on (also vanilla frames), and the
deferral ends after a bracket of the same record in which the vanilla probe left the record visible (`record+0x30`
non-zero, one validated read per bracket while deferred) with a `core` body at the sun's uv and no unclassifiable body
(`sun_occlusion_released ... note=body_classified_vanilla_visible`), so a classifiable chain (the stock vs_2_0 one) keeps
partial occlusion. Device Resets keep it. Rows: `sun_lens_body_unclassifiable ... note=blocked_vanilla_probe` on the
pair's first scan and on each deferral's start (`note=left_untouched` without a single record); deferral-start and
release rows are capped at 8 each per session (`Deferral::row_cap`). `block()` itself is unchanged, but a deferred
body's refusal never calls it: `prepare_lens` skips `block()` / `report_pass(false)` for every refusal of a deferred draw,
including `routed_draw` and `state_unknown`, so such a draw is dropped for the frame and handed to the releasable
deferral instead of the process-wide block. The ghost path is unchanged. FL: the code is fenced `#if !X3M_EDITION_FL`
like the install. Performance: the probe hook (`decide_probe`) grows from 605 to 613 instructions (+8, one integer compare
on the path); the visibility pass still runs every frame while the record is deferred (its fraction is unused then).

### Review (same day): three findings, F1 fixed

- **F1 (fixed): false-lift oscillation.** The first version lifted on any core-only bracket. With the sun hidden by the
  vanilla probe, another light's classifiable core within 0.005 u of the sun's uv lifted the deferral; the next frame was
  answered, the SM3 body drew, the bracket was dropped and the deferral restarted: a two-frame cycle with two unthrottled
  rows per cycle and the other flares at half rate (reviewer's driver: `answers=01101010101 blocks=5 logs=5 releases=4`).
  Now the lift also needs the vanilla probe to have left the record visible, i.e. the sun's own chain is drawn in that
  bracket, so its bodies decide; a hidden sun never lifts. No K-bracket fallback: a hidden sun's bracket says nothing
  about its chain. Rows capped at 8 per kind per session.
- **F2 (open): attribution by bracket, not by draw.** Any unclassifiable body of any record in a bracket with one sun
  record is charged to the sun (an SM3 ship flare would defer a classifiable sun). It errs towards vanilla. A refused SM3
  body has no known centre (the scan supplies the matrix register), so a per-draw test needs the SM3 scan below.
- **F3 (open): `cache_full` counts as unclassifiable and never lifts** while the scan cache stays full (every new pair is
  unscannable then); same direction (vanilla).

| Check | Result (measured) |
| --- | --- |
| `cmake --build build-ap -j4 2>&1 \| grep -ci warning` / same for `build-fl` (MinGW i686, RelWithDebInfo) | 0 / 0 |
| `python3 verification/results/run216-sun-occlusion/fl_object_compare.py BASE NEW build-fl` over motion_output, sun_occlusion, sun_occlusion_pass, loader, capture (BASE = main `bf6238ee`, NEW = the review-fix commit, both archive exports) | 5 of 5 identical (stripped). First version: an incremental FL build vs its parent, 79 of 80 objects identical, `proxy_identity.cpp.obj` differs |
| same script, `--count decide_probe` on `build-ap` `src/proxy/sun_occlusion.cpp.obj` | 605 -> 613 instructions |
| `python3 verification/probe/check_no_x87.py build-ap/d3d9.dll` | 1142 reachable functions, 0 violations |
| `/usr/bin/python3 verification/probe/run_host_suite.py --modules test_sun_occlusion test_logging_tiers` | 28 + 17 tests, 0 failing (`test_an_unclassifiable_sun_body_hands_the_record_to_the_vanilla_probe_until_it_classifies`: one block and one row per deferral, none for a classifiable chain, held through a hidden chain, a mixed bracket, a hidden-sun core and a Reset, lifted by a vanilla-visible core-only bracket, the F1 scenario, the row cap, per record) |
| `python3 verification/results/run216-sun-occlusion/oscillation.py FIRST_FIX_TREE REVIEW_FIX_TREE` (F1 scenario) | first fix `answers=01101010101 blocks=5 logs=5 releases=4`; review fix `answers=01100000000 blocks=1 logs=1 releases=0` |

Open: SM3 support for the sun's own chain (the scan and the clip wrap for a vs_3_0 / ps_3_0 pair; this pair is the
standard-lighting family, whose view-projection is c24-c27 per `docs/architecture/linear-distance-fade-region.md`, section
2), which would restore partial occlusion on this install and allow per-draw attribution (F2); until then the sun is
hidden all or nothing as in vanilla. Skipping the visibility pass while deferred (cost not measured). F2, F3 above.
Not covered: the Wine fixture (no run in this change), the flight.

## 2026-11-02: lens-flare gain through the fog (both editions; worktree, not installed, not flown)

FL run 13 showed the sun and its flare plainly through The Hole's fog (`verification/results/fl-run13-sun-fog/`): the
lens draws come after the fog composite and nothing attenuates them. Now, with the volumetric fog configured, capture
installs the lens bracket (`sun_occlusion::initialize(lens_gain_wanted || volumetric_fog_requested)`, observe-only
redirects when no sun-occlusion variable is set; row `lens_flare_gain … fog=1`), and `sun_occlusion_begin` sets the
bracket's law to `law(G × T_sun)`: `T_sun` = the Rec. 709 luminance of the engine plumes' fog law
(`engine_plumes_fog`) evaluated at the look's column cap (`renderer::fog_sky_lens_gain`, clamped to [0, 1]), `G` the
configured `X3M_LENS_FLARE_GAIN`. A frame without the density composite (fog off, a clear sector, filling) has the
law off: `T = 1` and the gain is `G` exactly. The BLENDFACTOR cap is kept from attach, so a fogged `G × T` in (0, 1)
works with `G = 1`; the blend shadow is fed while the fog gain is configured. The `--perf` row `lens_flare_gain_frame`
(every 300 frames while the applied gain is below 1) gains `base=` (G), `fog=` and `fog_t=` (the last bracket's T).
Without the fog nothing changes in behaviour: `lens_gain_fog_` is false, the law is the configured one and the
draw path is untouched; the `lens_flare_gain` and `lens_flare_gain_frame` rows carry the extra fields. The Hole at
run 13's inputs predicts `fog_t` ≈ 0.58 (inferred, `sky_transmittance.py`, plain column). Host case:
`test_fl_sun_occlusion.FogLensGain` (law off: `t=1`, gain `G` for G 1 / 0.6 / 0; T 0.5 at the cap gives 0.5 G; a
tinted fog gives the luminance; the FL bracket reaches the same `sun_occlusion_begin`). Evidence and the AP object
comparison: [FL ledger, "Step 9"](farnhams-legacy.md). Open: the gain applies to every lens draw of the bracket
(other records' flares take the sky's transmittance too); the distance-mode column (`--fog-distance 3`) is longer
than the look's cap (0.49 vs 0.58 in The Hole), not used here.

## 2026-11-02: Own sun step 1 — host evidence (AP minimal mode; worktree, not installed, not flown)

Design: [own-sun-pass.md](../architecture/own-sun-pass.md) (step 1; as built in its section 14). `--own-sun on`
(`X3M_OWN_SUN=1`, builtin and launcher default off) draws a procedural disc and corona as the first act of the
temporal resolve's stage bracket (after the fog, RT1/RT2 unbound, FP16 RT0), placed from the sun light poll with the
jittered scene projection, clipped by the lane's nine-tap knight kernel, multiplied by the fog's far transmittance
scalar (`renderer::fog_sky_lens_gain` at gain 1); the flare probe answers "hidden" for the sun's eligible record in
an own-sun frame, before the readiness machine and the latch. Ctrl+Shift+F9 flips both per frame. FL: one
`own_sun status=unavailable edition=fl` row, nothing else.

| Check | Command | Result |
| --- | --- | --- |
| Builds | `cmake --build build-ap -j4`, `cmake --build build-fl -j4` (MinGW i686 16.2, RelWithDebInfo) | 0 warnings each (measured) |
| Object scope | `verification/results/own-sun-pass/compare_objects.py <baseline d3d9.dir> <build d3d9.dir> <scratch>` (debug stripped, baseline = main `59944c32`) | FL 81 objects, 3 differ: capture, config, proxy_identity; AP 122, +`own_sun_pass`, 5 differ: capture, config, motion_output, proxy_identity, sun_occlusion (measured) |
| x87 audit | `python3 verification/probe/check_no_x87.py build-ap/d3d9.dll` | PASS, 1,146 reachable functions (measured) |
| Programs | `python3 tools/shaders/generate_own_sun_programs.py --check` (native host, compile.exe through WSL interop) | PASS; vs 43 words, ps 339 words (measured) |
| Native validator | `python3 verification/results/own-sun-pass/native/validate.py <scratch>` -> [result.json](../../verification/results/own-sun-pass/native/result.json) | vs 2 slots, ps 70 slots; `end=00000000`, 0 messages each (measured) |
| Host test | `run_host_suite.py --modules test_own_sun test_sun_occlusion test_logging_tiers test_launcher_defaults test_fl_sun_light_poll test_fl_sun_occlusion test_config_schema` | 7 modules, 101 tests, 0 failing (measured) |
| Config | `python3 tools/config/generate.py --check` | PASS, 297 settings, 136 in the template (measured) |
| CPU cost | `python3 verification/results/own-sun-pass/cpu_cost.py <scratch>` (i686 `-O2 -msse2 -mfpmath=sse`, native, 5 x 1,000,000 frames) | median 37.0 ns per frame for projection, fog scalar, radiance, quad and constants (measured); the ~26 device calls per drawn frame are not in it |

`test_own_sun` (production `own_sun_core.h` through `verification/probe/own_sun_host.cpp`): projection of 60 random
cameras and suns against a double restatement (u, v, jittered NDC, half extents within 2e-5); jitter moves only the
quad; behind, offscreen, camera and size refusals; radius 0.04 x 1280 = 51.2 px, 1.48 px draws nothing, 1.52 px
draws, out-of-range radius keeps 0.04; fog scalar exactly 1 without the composite and within 2e-5 of the analytic
law, 0 for an opaque column (draws nothing); centre texel `K lerp(c, 1, 0.6) T`, 0 at the quad edge, 7/9 with two
covered taps; answer 1 only for active and eligible; the hotkey's held chord is one press; the constants' eight
offsets are exactly the knight moves and give the edge profile (1, 7/9, 5/9, 4/9, 2/9, 0); the compiled CTAB is
c0..c4 + s0 as `pixel_constants` writes them; launcher default `X3M_OWN_SUN=0`, inherited values dropped, refusals.
`test_sun_occlusion` pins the answer's place in `decide_probe`; `test_logging_tiers` the rows' tiers;
`test_launcher_defaults` the default; `test_comparison_hotkeys` the chord (its two `bloom_strength` assertions fail on
main since 9813e2c0, unrelated).

Not run: no GPU fixture (section 11's real-device cases) and no Wine hook fixture for the new answer branch (no Wine on
this host); the stage's GPU cost is inferred at about 0.05 ms at 5120x1440 (1.5 Mpx x 70 slots x 0.135 ns + the fixed
draw cost). The first flight is the device test.

Flight (AP, `--own-sun on --debug`): start rows `sun_occlusion ... patched=1 reason=ok ... own_sun=1` and
`own_sun status=ready edition=ap requested=1 reason=ok`; per device `own_sun_device attached=1 ... vs_slots=2
ps_slots=70 max_ps_slots=` and `own_sun_state armed=1 reason=armed sun=own`; every 10 s `own_sun_frame active=1 key=1
armed=1` with `answered` >= `active_frames`, `drawn` > 0 with the sun on screen, `poll=ok`, `skip=none`, `radius_px`,
`colour`, `fog_t` (< 1 in a fogged sector); after Ctrl+Shift+F9 `own_sun_toggle on=0` and `active=0 answered=0`.
F8 bursts: the sun behind a hull edge (own sun, then toggled to vanilla), and the sun clear in a fogged sector (both
states). No `own_sun_failed`, no `own_sun_device attached=0`.

### Own sun step 1 — review fixes (2026-11-02; rebased onto main `2f47e5a5`; not flown)

Review of the step-1 commit, applied in one commit ([own-sun-pass.md](../architecture/own-sun-pass.md) section 14):
F1 the stage draws only in a frame whose main-view eligible probe the own sun answered "hidden" (planets leave the
lane at the sentinel; a failed eligibility kept a double sun); armed, active frames without it count `no_probe=` and
write one `own_sun_no_probe` row. F2 the sun poll moved into the arming: not Ok leaves the stage unarmed
(`own_sun_state reason=poll`), so the next probe forwards; a camera or size refusal of the projection clears the
readiness. F3 section 2 corrected (below); `own_sun_frame` carries `ev=` and `ev_limit=`. F4 disc
`K lerp(c / m, 1, 0.6) m T`, F5 corona `0.8 (K / 8) c T`, K = 0 draws nothing and requests no answer
(`own_sun status=unavailable reason=radiance_zero`). F6 `sun_disc_radius` is a fraction of the viewport height
(default 0.0667, 51 px at 768 rows, 96 px at 1440). F9 the hotkey note says the "off" state is the sun-occlusion-modified
chain and the chord is not focus-limited.

| Check | Result |
| --- | --- |
| Builds `build-ap` / `build-fl` (`-j4`) | 0 warnings each (measured) |
| `compare_objects.py` against main `2f47e5a5` | FL 81 objects, 3 differ (capture, config, proxy_identity); AP 122, +`own_sun_pass`, 5 differ (capture, config, motion_output, proxy_identity, sun_occlusion) (measured) |
| `check_no_x87.py build-ap/d3d9.dll` | PASS, 1,146 (measured) |
| Host modules test_own_sun, test_sun_occlusion, test_logging_tiers, test_launcher_defaults, test_fl_sun_occlusion, test_config_schema | 99 tests, 0 failing (measured) |
| Programs | unchanged (vs 43 / ps 339 words, validator result as above) |
| CPU side, `cpu_cost.py` | median 36.1 ns per frame (measured) |
| Meter, `verification/results/own-sun-pass/meter_exposure.py` | 1280x768: the disc core covers 36-41 of 3,840 tiles (0.94-1.07 %; p99 needs 39); 1920x1080 0.81-0.85 %, 5120x1440 0.49-0.76 % with the height radius (1.96-2.23 % with the old width radius); a white K = 8 decodes (gamma 2.2) to 97, clipped 64: ev_limit -2.13 EV while the disc holds p99 (+0.87 EV under the identity decode); K <= 3.39 keeps >= 0 EV, K <= 2.25 leaves the +1.3 EV cap (measured, offline) |

The review's "+0.87 EV" assumed the identity decode; the default decode is gamma 2.2, so at 1280x768 the guard would
pull the exposure to about -2.1 EV with the sun in view. The flight's `ev=` / `ev_limit=` decide; the meter is
unchanged. Flight additions: `no_probe=0` (or near 0) in frames with the sun in view, `ev` / `ev_limit` with and without
the sun (Ctrl+Shift+F9), and the sun setting behind a planet (the disc must vanish with the engine's verdict).

## Run 219 — own sun step 1 installed (2026-11-02, dev candidate, no release)

Merge `b1f7a8c3` (step 1 `97ed0320` + review fixes `3d5049f9`, reviewed Opus high: fix first, fixes applied), unstripped
`build-ap` DLL `50b8b2f3…` (0 warnings, x87 PASS, 99 host tests green) installed into `C:\X3`, rollback
`rollback-d3d9\d3d9.dll.run218`; `C:\X3\x3m.ini` gets `own_sun = 1` under `[graphics]` (previous copy `x3m.ini.run218`),
everything else unchanged ([record](../../verification/results/run219-candidate-install.json)). Flight: Ctrl+Shift+F9
flips between the own sun and the engine chain per frame; two F8 pairs (own / engine): the sun partly behind a station
strut or hull edge, and the sun in clear view in a fogged sector. Rows: `own_sun status=ready`, `own_sun_device attached=1
vs_slots=2 ps_slots=70`, `own_sun_state armed=1 sun=own`, `own_sun_frame active=1 … drawn>0 no_probe≈0 ev= ev_limit=`,
`own_sun_toggle` on the chord; wrong: `own_sun_failed`, `attached=0`, `no_probe` rising with the sun in view (probe order
assumption wrong), a double sun or no sun for more than one frame at a toggle. Kill criterion: looks worse than vanilla
or shows through a hull → `own_sun = 0`.

### Own sun step 1b — the look after Run219 (2026-11-02; on main `78bbbacc`; not flown)

Run219 (measured, `/mnt/c/X3/x3m.log`): `own_sun_device attached=1 ps_slots=70 max_ps_slots=4096`; occlusion accepted;
look rejected (about 4x too big, too bright, a flat disc, white instead of vanilla's yellowish sun). With the sun in view
`ev` fell to -1.02 and `ev_limit` to -0.92 (frames 12140-14370), against +0.7..+3.9 without it. The three `no_probe`
windows (701, 1633 and 962 frames ending at frames 2571, 4519 and 39028) are not hotkey windows: hotkey-off frames are
inactive and never count, and the last two windows hold no `own_sun_toggle`; they are frames with the sun outside the main
view, which the engine's collector does not probe. Fogged sector `fog_t=0.8861`, colour 0.8789,0.9961,0.7812.

Step 1b ([own-sun-pass.md](../architecture/own-sun-pass.md) section 14, "Step 1b"): a 9-17 px core at K = 2 with a 0.25
white mix, 16 thin rays of hashed length, two horizontal streaks and a faint halo, all glow in the polled colour exactly;
`sun_rays` (0..1) added.

| Check | Result |
| --- | --- |
| Builds `build-ap` / `build-fl` (`-j4`) | 0 warnings each (measured) |
| `compare_objects.py` against main `78bbbacc` | FL 81 objects, 2 differ (config, proxy_identity); AP 122, 5 differ (capture, config, motion_output, proxy_identity, own_sun_pass) (measured) |
| `check_no_x87.py build-ap/d3d9.dll` | PASS, 1,146 (measured) |
| `generate_own_sun_programs.py --check` | PASS; vs 43 words, ps 743 words (measured) |
| Native validator ([result](../../verification/results/own-sun-pass/native/result.json)) | vs 2 slots, ps 142 slots (D3DX: 141, 14 texture + 127 arithmetic); `end=00000000`, 0 messages each (measured) |
| Host modules test_own_sun, test_sun_occlusion, test_logging_tiers, test_launcher_defaults, test_fl_sun_light_poll, test_fl_sun_occlusion, test_config_schema | 104 tests, 0 failing (measured) |
| `generate.py --check` | PASS, 298 settings (measured) |
| CPU side, `cpu_cost.py` | median 36.8 ns per frame (measured) |
| Meter, `meter_exposure.py` | the core covers 0.10-0.16 % of the tiles at 1280x768, 0.05-0.22 % at 5120x1440 (p99 needs 1 %); K = 2 decodes to 4.6: ev_limit +1.67 EV even at p99; glow peak 0.764 decodes to 0.55 (measured, offline) |

`test_own_sun` adds: the glow at (225, 255, 200) keeps the hue ratio to 2e-6 over the whole quad; the halo never rises
outward; each spoke mirror-symmetric (6e-6); 16 ray reaches from 3.3 to 12.9 core radii (at least three >= 10, four
<= 4.5); glow peak < E = 1; streaks left/right equal and absent vertically; `sun_rays 0` removes rays and streaks, not
the halo; a core with 2 of 5 taps visible keeps 2/5 of its glow; the radius rule at 0.012 of the height; the 10-register
CTAB. GPU cost (inferred): 0.46 Mpx x 142 slots x 0.135 ns = 9 us at 5120x1440 plus the fixed draw, about 0.05 ms.
The Run219 `ev_limit` of -0.92 with the 96-px disc at 5120x1440 contradicts the tile model's "below p99"; the model
under-counts something there (open). Flight: `own_sun_frame` with `ev` near the no-sun windows' values and
`ev_limit` above +1.3 with the sun in view; the yellowish hue in the fogged sector; ray lengths irregular and fixed.

### Own sun step 1b — review fixes (2026-11-02; on main `66d2afd3`; not flown)

F1: the core's visible fraction (which carries the halo, rays and streaks) comes from 13 weighted taps instead of 5
equal ones: the centre, six at 0.45 and six at 0.9 core radii, weights `1 - 0.1 rho^2` normalised (0.0807 / 0.0791 /
0.0742): one tap moves the burst by at most 8.1 % instead of 20 %. 17 taps (6.2 %) compiled to 216 slots, over the
~200 budget; 13 was the allowed fallback. F2: each pixel sums the two nearest spokes; the ray term is continuous across
a spoke boundary (nearest spoke alone: 0.038 glow units x (1 - D) at r 0.87; summed: 0 in `law_checks.py`, 3.3e-4 in
the host driver's +-2e-4 rad finite difference). F3 (correction of the step-1b row above): tiles below the p99 index
are not without effect, they shift which tile is the p99 one. The new core adds 1-4 tiles at 5120x1440 and decodes to
at most 4.6, so `ev_limit` stays at the scene's own value within a few tiles' shift.

| Check | Result |
| --- | --- |
| Builds `build-ap` / `build-fl` (`-j4`) | 0 warnings each (measured) |
| `compare_objects.py` against main `66d2afd3` (step 1b and these fixes together) | FL 81 objects, 2 differ (config, proxy_identity); AP 122, 5 differ (capture, config, motion_output, proxy_identity, own_sun_pass) (measured) |
| `check_no_x87.py build-ap/d3d9.dll` | PASS, 1,146 (measured) |
| `generate_own_sun_programs.py --check` | PASS; ps 996 words (measured) |
| Native validator ([result](../../verification/results/own-sun-pass/native/result.json)) | vs 2 slots, ps 197 slots (22 texture); `end=00000000`, 0 messages each (measured) |
| Host modules test_own_sun, test_sun_occlusion, test_logging_tiers, test_launcher_defaults, test_fl_sun_light_poll, test_fl_sun_occlusion, test_config_schema | 106 tests, 0 failing (measured) |
| `generate.py --check` | PASS, 298 settings (measured) |
| CPU side, `cpu_cost.py` | median 36.7 ns per frame (measured) |
| `verification/results/own-sun-pass/law_checks.py` | arctangent max error 3.46e-6 rad (8.8e-6 spoke units); ray lengths 3.75-14.42 core radii, strengths 0.63-0.95; seam 0.0381 alone, 0 summed; tap weights sum 1.00000, largest step 8.1 % (measured, offline) |

`test_own_sun` adds `test_no_seam_between_spokes` (alone > 0.02, summed < 1e-3) and `test_core_visibility_taps` (12
ring offsets at 0.45 and 0.9, the weights' ratios and sum, largest step < 8.5 %). GPU cost (inferred): 0.46 Mpx x 197
slots x 0.135 ns = 12 us at 5120x1440 plus the fixed draw, about 0.05 ms.

## Run 220 — own sun step 1b look installed (2026-11-02, dev candidate)

Run 219 flown: occlusion accepted ("works and is better than vanilla"); the disc was too big and too bright (the meter
pulled `ev` to -1.0 with the sun in view), white instead of the sector's yellow, and the user wants a star with rays
(reference art). Step 1b (`fefb0411`, review fixes `403f10c7`; reviewed Opus medium: merge): core radius 0.012 of the
height (17 px at 1440), radiance 2, 16 rays of uneven length plus two horizontal streaks and a faint halo in the
polled colour (core white mix 0.25), `sun_rays` setting, core-visibility from 13 weighted taps, two nearest spokes
summed; ps 197 slots, validator 0 messages. Merge `403f10c7` built unstripped (`build-ap` `9feed01b…`, 0 warnings, x87
PASS, 138 host tests green) and installed into `C:\X3`, rollback `rollback-d3d9\d3d9.dll.run219`
([record](../../verification/results/run220-candidate-install.json)); the same DLL carries the fade-owner shadow
receiver/caster change (fill-light ledger). Flight: the two F8 pairs again (hull edge across the sun; fogged sector),
Ctrl+Shift+F9 for the engine chain; watch for burst flicker while parked with a hull edge across the core, ray shimmer
under TAA, and `own_sun_frame ev=`/`ev_limit=` no longer dropping with the sun in view.

**Run 220 flown (2026-11-02).** User on the step 1b star: "like a painted circle with rays, not a sun or something
belonging to the scene"; wants a big bright halo (about 3x) with falloff and dimmer rays, and first a survey of how
modern space games render the sun (`docs/reference/sun-rendering-survey.md`, in progress) before the next look
iteration. Occlusion and the shadow receiver change accepted.

## 2026-11-02: Own sun step 1c — host evidence (on main `47a16590`; not flown)

[own-sun-look.md](../architecture/own-sun-look.md) implemented as decided (own-sun-pass.md section 14, "Step 1c"): the
core unchanged; a Gaussian corona (sigma 1.5 R, 0.6) and a Lorentzian halo (half brightness at 6 R, 0.3, windowed at
24 R); six soft rays at 0.15 (hashed lengths 3.50, 9.77, 11.17, 4.14, 12.92, 13.13 R; width max(3 px, 0.2 R), no taper;
`exp(-r/L) saturate(1 - r/L)^2` along; two nearest spokes summed); the streak pair and its constants deleted; quad
48 x 48 R; visibility fraction, per-pixel clip, fog scalar, screen-fixed rays and the four settings unchanged. The
stale "stabiliser E = 1" wording in own-sun-pass.md sections 2, 7 and 14 and in the shader and core headers now names
the bound's live purpose (meter guard, AgX contrast).

| Check | Result |
| --- | --- |
| Builds `build-ap` / `build-fl` (`-j4`) | 0 warnings each (measured) |
| `compare_objects.py` against main `47a16590` | FL 81 objects, 1 differs (proxy_identity); AP 122, 3 differ (motion_output, proxy_identity, own_sun_pass) (measured) |
| `check_no_x87.py build-ap/d3d9.dll` | PASS, 1,146 (measured) |
| `generate_own_sun_programs.py --check` | PASS; ps 946 words (measured) |
| Native validator ([result](../../verification/results/own-sun-pass/native/result.json)) | vs 2 slots, ps 184 slots (22 texture); `end=00000000`, 0 messages each (measured) |
| Host modules test_own_sun, test_sun_occlusion, test_logging_tiers, test_launcher_defaults, test_fl_sun_light_poll, test_fl_sun_occlusion, test_config_schema | 107 tests, 0 failing (measured) |
| `generate.py --check` | PASS, 298 settings (measured) |
| CPU side, `cpu_cost.py` | median 37.8 ns per frame (measured) |
| `glow_meter.py` (now reads the header's constants by name) | glow max outside the core 0.781 (corona 0.385, halo 0.291, rays 0.106); axis 0.676, 0.368, 0.244, 0.132, 0.068, 0.034, 0.011, 0.002 at 1, 2, 3, 6, 9, 12, 16, 20 R; 5120x1440 (1,840 tiles, p99 needs 19): 0 tiles at the K-2.25 luminance, 1-4 above engine 1, 2-4 above the key, 20-24 lit (1.09-1.30 %); 1280x768 (3,840, p99 needs 39): 0, 1-4, 6-9, 96-100 lit (2.50-2.60 %); the brightest sun tile gives ev_limit +1.67 EV, a glow tile >= +3.87 EV (measured, offline) |
| `law_checks.py` (N = 6) | arctangent 3.46e-6 rad; seam 0.0003 with the nearest spoke alone, 0 summed; ray width 0.200 R = 3.46 px at 1440 rows; tap weights sum 1.00000, largest step 8.1 % (measured, offline) |

`test_own_sun` (22 tests): glow maximum outside the core 0.77-0.781 at K = 2; no rise outward on 72 radial lines;
corona and halo on the axis equal the note's laws (halo 0 at 24 R); six spokes with the note's lengths, reaches ordered
by length, peaks under 0.15; no streak term in the header or program, the horizontal axis carries no ray; `sun_rays 0`
keeps corona and halo; ray width 3.0 px at 768 rows, 3.46 px at 1440 and 10 px for a 50-px core; hue ratio
(225, 255, 200) kept to 2e-6; the 13 taps unchanged. GPU cost (inferred): 0.69 Mpx x 184 slots x 0.136 ps = 17 us at
5120x1440 plus the fixed draw. The lit-tile shift of the key rule is unmodelled against a real scene; the flight's
`own_sun_frame ev=/ev_limit=` with and without the sun decides (the note's kill rule: `ev` lower by > 0.25 EV with the
sun in view).

## Run 221 — own sun step 1c look installed (2026-11-02, dev candidate)

Design `docs/architecture/own-sun-look.md` (survey-led, user-approved): Gaussian corona (σ 1.5 R, 0.6) + Lorentzian
halo (half at 6 R, 0.3, window 24 R), six soft rays (0.15, hashed 3.5–13.1 R, width ≥ 3 px), streak pair removed,
quad 48 R; settings and defaults unchanged. Step 1c `13692fc8` (reviewed Opus medium: install; finding 1: with six
spokes at offset 0.5 two spokes sat on the vertical axis, a vertical line through the sun → `ray_offset` 0.25 in
`01a5e52c`; finding 2 recorded: a sub-0.05 jump at a spoke's centre line only below the default radius). ps 184
slots, validator 0 messages; `glow_meter.py`: glow max outside the core 0.781, 20–24 lit tiles at 5120×1440, worst
sun-tile `ev_limit` +1.67 EV. Merge `01a5e52c` built unstripped (`9feed01b` → `15ac0911…`, 0 warnings, x87 PASS, 99
host tests green) and installed into `C:\X3`, rollback `rollback-d3d9\d3d9.dll.run220`
([record](../../verification/results/run221-candidate-install.json)). Flight: the two F8 pairs with Ctrl+Shift+F9;
kill rule: worse than the engine chain in either sector, or `own_sun_frame ev=` more than 0.25 EV lower with the sun
in view than without, or rays shimmering while parked.

**Run 221 flown (2026-11-02).** User: "close!". Four adjustments requested (step 1d): more rays of visibly different
length (only four equal-length rays read as an X), the halo/blob radius down about 30 %, the core 25–30 % dimmer,
and the glow next to the core brighter so the core does not stand out (reference: Everspace 2's blown-out near-core
region). In progress on a fresh implementer.

**Own sun tuning lab (2026-11-02).** `tools/effects/own_sun_lab.html`, the law in WebGL2 with defaults generated from `own_sun_core.h` ([own-sun-look.md, "The tuning lab"](../architecture/own-sun-look.md#the-tuning-lab)); `test_own_sun_lab` (5 tests): `--check` PASS; the page JS law under node against the C++ reference at 1, 2, 3, 6 R over six angles and two suns plus three core/clipped pixels (51 points): worst |JS − C++| 2.7e-7 (asserted < 1e-4); HUD glow max 0.7811 (polar scan) against the reference field scan 0.7798 at 0.02 R (white, K = 2; glow_meter.py 0.781).
## 2026-11-02: Own sun step 1d — host evidence (on main `02122991`; not flown)

The four Run221 items as constants ([own-sun-look.md, "Step 1d"](../architecture/own-sun-look.md)): eleven spokes at
offset 0.125 (>= 4.09 deg off the screen axes, no opposite pair), lengths `13 (0.15 + 0.85 frac(0.618034 k + 0.3))`
beyond the core's edge (2.04-12.09 R, tips <= 13.1 R) with the along-profile starting at the core's edge (`ray_start`
1, c9.x), strengths `0.6 + 0.6 h2` (0.64-1.12); corona 0.9 at sigma 1.05; halo half at 4.2, window and quad 17; new
`core_gain` 0.72 on the core term only. Ray gain, width floor, two-spoke sum, taps, settings unchanged.

| Check | Result |
| --- | --- |
| Builds `build-ap` / `build-fl` (`-j4`) | 0 warnings each (measured) |
| `check_no_x87.py build-ap/d3d9.dll` | PASS (measured) |
| `generate_own_sun_programs.py` (regenerated), then `--check` | PASS; ps 922 words (946 before), vs 43 unchanged (measured) |
| Native validator ([result](../../verification/results/own-sun-pass/native/result.json)) | vs 2 slots, ps 178 slots (184 before); `end=00000000`, 0 messages each (measured) |
| Host modules test_own_sun, test_sun_occlusion, test_logging_tiers, test_launcher_defaults, test_config_schema | 99 tests, 0 failing (measured) |
| CPU side, `cpu_cost.py` | median 40.3 ns per frame (37.8 before; one multiply more, within run spread 38.6-45.3) (measured) |
| `glow_meter.py` | glow max outside the core 0.813 at 1440 rows, 0.826 at 768 (corona 0.363, halo 0.282, rays 0.167 / 0.181); between spokes 0.675, 0.381, 0.262, 0.187, 0.076, 0.028, 0.008 at 1, 1.5, 2, 3, 6, 9, 12 R; 5120x1440: 0 tiles at the K-2.25 luminance, 1-4 above engine 1, 9-12 lit (0.49-0.65 %); 1280x768: 0, 1-4, 45-52 lit (1.17-1.35 %); brightest sun tile L 2.23 (the core, 1.44 engine) -> `ev_limit` +2.72 EV, a glow tile >= +3.87 EV, both above the +1.3 EV cap; quad 0.35 / 0.10 Mpx (measured, offline) |
| `law_checks.py` (N = 11) | arctangent 3.46e-6 rad; nearest spoke to an axis 0.125 spoke = 4.09 deg; nearest pair to opposite 0.5 spoke; seam 0.0110 with the nearest spoke alone, 0 summed; width 3.46 px at 1440 rows; tap weights sum 1.00000, largest step 8.1 % (measured, offline) |
| `own_sun_host field` / `seam` (1440 rows, step 0.02) | glow_max 0.81258, hue error 0, no rise outward, asymmetry 3.1e-7; every ray's peak = 0.15 x strength at the core's edge; two-spoke seam 5.3e-5 (measured) |
| `ray_visibility.py` (contrast model, ray >= 10 % of corona + halo) | step 1c: 4 of 6 rays visible (reach 0, 4.4, 6.9, 0, 8.0, 7.7 R: Run221's X); step 1d: 11 of 11 (2.0-10.9 R) (inferred) |

Display at the +1.3 EV cap (AgX table, inferred): core 0.95 (1c 0.98), 1 R 0.79 (0.82), 1.5 R 0.59 (about 0.69), 6 R
0.09 (1c 0.23): the visible glow ends near 6 R instead of 9 R. Item 4 as specified leaves the core-to-1 R display step at
0.16 (narrower corona offsets the higher gain); the note records what would close it. GPU cost (inferred): 0.35 Mpx x
178 slots x 0.136 ps = 8.5 us at 5120x1440. Flight: the Run221 comparison (two F8 pairs, Ctrl+Shift+F9) and the
`own_sun_frame ev=/ev_limit=` kill rule.

### Own sun step 1d — item 4 follow-up (2026-11-02; on `4413eada`; not flown)

The first pass (`corona_gain` 0.9) left the core-to-1 R display step at 0.16. Now `corona_gain` 2.15 (sigma 1.05 kept)
and `core_inner` 0.4. New cap rule replacing "glow < 1.0 engine outside the core": every pixel under engine 2.25 (the
K-2.25 luminance 5.96) and the worst sun tile's `ev_limit` >= +1.3 EV; `glow_meter.py` prints PASS/FAIL for it.
Program bytecode unchanged (ps sha `e0e5b5c3…`, 922 words; only the provenance record's source hash moved for a comment),
so the validator result stands (178 slots, 0 messages).

| Check | Result |
| --- | --- |
| Builds `build-ap` / `build-fl` (`-j4`), `check_no_x87.py` | 0 warnings each, PASS (measured) |
| `generate_own_sun_programs.py --check` | PASS (measured) |
| Host modules (the five) | 99 tests, 0 failing (measured) |
| `own_sun_host field` (1440 rows) | glow_max 1.31720, hue error 0, no rise outward; two-spoke seam 5.6e-5 (measured) |
| `glow_meter.py` | glow max outside the core 1.317 (1440) / 1.331 (768); between spokes 1.179, 0.543, 0.295, 0.187, 0.076 at 1, 1.5, 2, 3, 6 R; brightest pixel 1.64 / 1.67 engine (inside the soft edge, core 1.44); 0 tiles at the K-2.25 luminance; lit 9-12 (5120x1440), 45-52 (1280x768); worst sun tile `ev_limit` +2.30 EV (5120x1440), +2.24 EV (1280x768); cap rule PASS at both (measured, offline) |
| Display at +1.3 EV (AgX table) | core 0.95, 1 R 0.92, 1.5 R 0.72, 2 R 0.49, 6 R 0.09 (inferred) |

## Run 222 — own sun step 1d installed (2026-11-02, dev candidate)

Step 1d (`4413eada` + item-4 follow-up `92e521ac`, merge `2a4a77cf6`; constants plus the ray start at the core edge, ps 178
slots, validator 0 messages; no separate review: a constants change over the reviewed step 1c pass, pinned by
`test_own_sun` and `glow_meter.py` PASS at both resolutions, worst sun-tile `ev_limit` +2.30 EV at 5120×1440) built
unstripped (`build-ap` `e0e351c5…`, 0 warnings, x87 PASS, 99 host tests green) and installed into `C:\X3`, rollback
`rollback-d3d9\d3d9.dll.run221` ([record](../../verification/results/run222-candidate-install.json)). The tuning lab
`tools/effects/own_sun_lab.html` (`3e81f60a9`) is merged; its re-port to the 1d law is pending. Flight: the two F8
pairs with Ctrl+Shift+F9; expected: eleven rays of different length, glow ending near 6 R, the core edge blending into
a bright region to ~1.5 R; same kill rule as run 221.
