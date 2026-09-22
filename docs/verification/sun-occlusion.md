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
